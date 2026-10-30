#pragma once

#include <stdint.h>

#include "Cpu6502.h"
#include "Crtc6845.h"
#include "Ins8154.h"
#include "PrinterPort.h"
#include "SerialTape.h"
#include "config.h"
#include "cos_rom.h"

// Acorn System 2: tarjeta de CPU 6502 + tarjeta VDU (6845 + teletexto
// SAA5050) + teclado ASCII (INS8154, puerto B). Mapa (contrastado con el
// desensamblado de cos.ic7 y con el driver acrnsys.cpp de MAME):
//
//   0000-03FF  RAM (1 KB)                       $0000-$00FF pagina cero, $0100 pila
//   0400-07FF  RAM de video (40 x 24/25 caracteres teletexto, 1 KB)
//   0800-08FF  6845 (R/S = A0)
//   0C00-0C0F  VIA 6522 de impresora            (segun el desensamblado del COS)
//   0E00-0E7F  INS8154: E/S   0E80-0EFF: 128 bytes de RAM
//   2000-...   RAM de expansion (SYS2_EXT_RAM_KB, hasta 8 KB)
//   F800-FFFF  COS (cos.ic7)
//   resto      sin decodificar: lee 0xFF
//
// Teclado: puerto B del INS8154 = codigo ASCII de 7 bits + strobe en el bit 7.
// El strobe se ve a 1 en la primera lectura de la tecla y se borra al leerlo,
// como en el driver de MAME (el COS lo lee dos veces: BIT/BPL y LDA/BMI).
// Cinta: el COS la mueve bit a bit por PA6/PA7 con retardos de 300 baudios.
// En vez de emular la senal, run() intercepta las rutinas de la ROM que hablan
// con el hardware (ver trap) y las conecta a una SerialTape; el resto del
// COS (formato de bloque, nombres, suma de comprobacion) se ejecuta de verdad.
class System2Machine : private Ins8154::Pins {
public:
    static const uint16_t kRamSize = 1024;
    static const uint16_t kVideoRamBase = 0x0400;
    static const uint16_t kVideoRamSize = 1024;
    static const uint16_t kExtRamBase = 0x2000;
    static const uint16_t kExtRamSize = (uint16_t)(SYS2_EXT_RAM_KB * 1024);
    static const uint8_t kKeyQueueSize = 16;

    System2Machine();

    void reset(bool cold);                  // cold: borra RAM/video; warm: solo la CPU (boton RESET)
    void run(uint16_t instructions);        // ejecuta hasta N instrucciones y atiende el teclado
    void attachTape(SerialTape* tape) { tape_ = tape; }   // sin cinta, LOAD/SAVE/CAT se quedan esperando

    // Puntos de cos.ic7 que se interceptan (analisis en README, "Cinta").
    static const uint16_t kCosPrompt = 0xF81D;          // prompt '*' del interprete de comandos
    static const uint16_t kCosWriteBlock = 0xFC00;      // SAVE: empieza un bloque (tono de guia + '****')
    static const uint16_t kCosCarrier = 0xFC4B;         // espera portadora / ESPACIO y lee los 4 '*'
    static const uint16_t kCosCarrierFound = 0xFC6D;    //   ...portadora detectada: lee y comprueba los 4 '*'
    static const uint16_t kCosCarrierAbort = 0xFC6B;    //   ...ESPACIO: SEC; RTS
    static const uint16_t kCosGetStart = 0xFCAB;        // OSBGET ($FCA6): tras STX $EB; PHP; LDX #8, espera el bit de arranque
    static const uint16_t kCosGetDone = 0xFCBD;         //   despues del bucle de 8 bits: JSR $FD41; PLP; suma al checksum; RTS
    static const uint16_t kCosPutBits = 0xFD2E;         // OSBPUT ($FD23): tras sumar el byte al checksum, empieza a sacar los bits
    static const uint16_t kCosPutDone = 0xFD3A;         //   despues del bucle de 8 bits: STX $0E20; PLP; LDX $EB; RTS
    static const uint16_t kCosDelay = 0xFD46;           // retardo de medio bit: STY $E9; SEI; bucle ~660 instrucciones; LDY $E9; RTS

    // --- Interfaz de memoria para Cpu6502<System2Machine> (todo en linea: ver Cpu6502.h).
    static const bool kTraps = true;
    uint8_t rd(uint16_t a);
    void wr(uint16_t a, uint8_t v);
    uint8_t fetchOp(uint16_t pc);       // codigo de operacion (ROM parcheada con 0x02 en los ganchos)
    uint8_t fetchByte(uint16_t pc);     // operandos (ROM original)
    uint8_t* lowRam() { return mem_; }  // $0000-$07FF: RAM + video
    uint8_t origOp(uint16_t pc);        // lo que hay de verdad en pc (sin parchear)
    HookResult trap(uint16_t pc);       // llamado por la CPU al llegar a un gancho

    // Lectura/escritura sin efectos de CPU, para tests y herramientas.
    uint8_t read(uint16_t a) { return rd(a); }
    void write(uint16_t a, uint8_t v) { wr(a, v); }

    // Cola de teclas (ASCII de 7 bits). Devuelve false si esta llena.
    bool queueKey(uint8_t code);
    uint8_t keyQueueFree() const { return (uint8_t)(kKeyQueueSize - key_count_); }
    bool keyboardIdle() const { return key_count_ == 0 && !kbd_strobe_; }

    const uint8_t* videoRam() const { return mem_ + kVideoRamBase; }
    const Crtc6845& crtc() const { return crtc_; }
    typedef Cpu6502<System2Machine> Cpu;
    Cpu& cpu() { return cpu_; }
    const Cpu& cpu() const { return cpu_; }

private:

    // Ins8154::Pins
    uint8_t readA() override { return 0x00; }          // PA7 (entrada de cinta) en reposo
    uint8_t readB() override;
    uint8_t peekA() override { return 0x00; }
    uint8_t peekB() override { return (uint8_t)(kbd_data_ | (kbd_strobe_ ? 0x80 : 0)); }
    void writeA(uint8_t) override {}
    void writeB(uint8_t) override {}

    void pumpKeyboard(uint16_t elapsedInsns);

    uint8_t readKbd();                      // puerto B del INS8154 ($0E21): el COS lo sondea sin parar
    uint8_t readSlow(uint16_t a);           // todo lo que no es RAM/video/ROM
    void writeSlow(uint16_t a, uint8_t v);

    SerialTape* tape_;
    Cpu cpu_;
    Ins8154 ins_;
    Crtc6845 crtc_;
    PrinterPort printer_;

    // RAM y video contiguas ($0000-$07FF). Estatica (el firmware tiene una unica
    // maquina): su direccion es una constante y la CPU no gasta registros en ella.
    static uint8_t mem_[kRamSize + kVideoRamSize];
#if SYS2_EXT_RAM_KB > 0
    uint8_t ext_ram_[kExtRamSize];
#endif

    uint8_t key_queue_[kKeyQueueSize];
    uint8_t key_head_, key_count_;
    uint8_t kbd_data_;
    bool kbd_strobe_;
    uint16_t kbd_gap_;
};

// ---------------------------------------------------------------------------
// Rutas rapidas (en linea): RAM/video y ROM; lo demas va a readSlow/writeSlow.
// ---------------------------------------------------------------------------
#define SYS2_INLINE inline __attribute__((always_inline))

SYS2_INLINE uint8_t System2Machine::rd(uint16_t a) {
    const uint8_t hi = (uint8_t)(a >> 8);
    if (hi < 0x08) return mem_[a];
    if (hi >= 0xF8) return pgm_read_byte(&kCosRom[a & 0x07FF]);
    if (a == 0x0E21) return readKbd();
    return readSlow(a);
}

SYS2_INLINE void System2Machine::wr(uint16_t a, uint8_t v) {
    const uint8_t hi = (uint8_t)(a >> 8);
    if (hi < 0x08) { mem_[a] = v; return; }
    if (hi >= 0xF8) return;  // ROM
    writeSlow(a, v);
}

SYS2_INLINE uint8_t System2Machine::fetchOp(uint16_t pc) {
    const uint8_t hi = (uint8_t)(pc >> 8);
    if (hi >= 0xF8) return pgm_read_byte(&kCosOps[pc & 0x07FF]);
    if (hi < 0x08) return mem_[pc];
    return readSlow(pc);
}

SYS2_INLINE uint8_t System2Machine::fetchByte(uint16_t pc) {
    const uint8_t hi = (uint8_t)(pc >> 8);
    if (hi >= 0xF8) return pgm_read_byte(&kCosRom[pc & 0x07FF]);
    if (hi < 0x08) return mem_[pc];
    return readSlow(pc);
}
