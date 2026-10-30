#include "System2Machine.h"

#include <string.h>

#include "cos_rom.h"

uint8_t System2Machine::mem_[kRamSize + kVideoRamSize];

System2Machine::System2Machine()
    : tape_(0), cpu_(*this), ins_(*this), key_head_(0), key_count_(0), kbd_data_(0),
      kbd_strobe_(false), kbd_gap_(0) {
    reset(true);
}

void System2Machine::reset(bool cold) {
    if (cold) {
        memset(mem_, 0, kRamSize);
        memset(mem_ + kVideoRamBase, 0x20, kVideoRamSize);
#if SYS2_EXT_RAM_KB > 0
        memset(ext_ram_, 0, sizeof(ext_ram_));
#endif
    }
    ins_.reset(cold);
    crtc_.reset();
    printer_.reset();
    key_head_ = key_count_ = 0;
    kbd_data_ = 0;
    kbd_strobe_ = false;
    kbd_gap_ = 0;
    cpu_.reset();
}

// Teclado: sin pasar por Ins8154/Pins (hoja, sin guardar registros).
uint8_t System2Machine::readKbd() {
    if (!kbd_strobe_) return kbd_data_;
    kbd_strobe_ = false;
    kbd_gap_ = SYS2_KBD_GAP_INSNS;
    return (uint8_t)(kbd_data_ | 0x80);
}

uint8_t System2Machine::readSlow(uint16_t a) {
    switch (a >> 8) {
        case 0x08: return crtc_.read((uint8_t)(a & 1));
        case 0x0C: if (a < 0x0C20) return printer_.read((uint8_t)(a & 0x0F)); break;
        case 0x0E:
            if ((uint8_t)a == 0x21) return readKbd();
            return ins_.read((uint8_t)a);
        default: break;
    }
#if SYS2_EXT_RAM_KB > 0
    if (a >= kExtRamBase && a < (uint16_t)(kExtRamBase + kExtRamSize)) return ext_ram_[a - kExtRamBase];
#endif
    return 0xFF;
}

void System2Machine::writeSlow(uint16_t a, uint8_t v) {
    switch (a >> 8) {
        case 0x08: crtc_.write((uint8_t)(a & 1), v); return;
        case 0x0C: if (a < 0x0C20) printer_.write((uint8_t)(a & 0x0F), v); return;
        case 0x0E: ins_.write((uint8_t)a, v); return;
        default: break;
    }
#if SYS2_EXT_RAM_KB > 0
    if (a >= kExtRamBase && a < (uint16_t)(kExtRamBase + kExtRamSize)) ext_ram_[a - kExtRamBase] = v;
#endif
}

uint8_t System2Machine::origOp(uint16_t pc) { return fetchByte(pc); }

uint8_t System2Machine::readB() { return readKbd(); }

bool System2Machine::queueKey(uint8_t code) {
    if (key_count_ >= kKeyQueueSize) return false;
    key_queue_[(uint8_t)((key_head_ + key_count_) % kKeyQueueSize)] = (uint8_t)(code & 0x7F);
    key_count_++;
    return true;
}

// Carga la siguiente tecla solo cuando la anterior ya se ha leido y ha pasado
// un margen: si el strobe de la tecla nueva apareciera entre el BIT y el LDA
// con que el COS lee el teclado (FD54-FD5C), la tecla anterior se perderia.
void System2Machine::pumpKeyboard(uint16_t elapsed) {
    if (kbd_gap_ > 0) kbd_gap_ = (kbd_gap_ > elapsed) ? (uint16_t)(kbd_gap_ - elapsed) : 0;
    if (!kbd_strobe_ && kbd_gap_ == 0 && key_count_ > 0) {
        kbd_data_ = key_queue_[key_head_];
        key_head_ = (uint8_t)((key_head_ + 1) % kKeyQueueSize);
        key_count_--;
        kbd_strobe_ = true;
    }
}

// Sustituye lo que el COS hace con el hardware de cinta (bits por PA6/PA7 con
// retardos de 300 baudios) por su efecto, dejando que el resto de la ROM
// (formato de bloque, nombres, suma de comprobacion) se ejecute de verdad. El
// COS depende del estado EXACTO que dejan estas rutinas (p. ej. el bucle del
// nombre en el SAVE hace "JSR $FFD1; INY; ROL A; CMP #$0D" y cuenta con que
// OSBPUT haya rotado A), asi que cada gancho salta solo el bucle de bits y
// reproduce lo que ese bucle deja en A, X y los flags:
//
//  $FC4B  espera de portadora (o ESPACIO = abortar). Con un bloque listo se
//         sigue en $FC6D, que lee y comprueba los 4 '*' con OSBGET; ESPACIO ->
//         SEC; RTS; sin bloque, la CPU espera (HOOK_WAIT).
//  $FCAB  OSBGET: la ROM ya hizo STX $EB; PHP; LDX #8. Se pone el byte en A,
//         X=0 (como deja el bucle) y se sigue en $FCBD (JSR $FD41; PLP; suma
//         al checksum $DC; RTS). Sin byte disponible, la CPU espera.
//  $FD2E  OSBPUT: la ROM ya sumo el byte al checksum (JSR $FCC1: A intacto, C =
//         acarreo de la suma). El bucle posterior rota (C:A) 10 veces hacia la
//         derecha por un anillo de 9 bits = una rotacion: A' = (A >> 1) | (C << 7);
//         deja X = $FF y sigue en $FD3A (STX $0E20; PLP; LDX $EB; RTS).
//  $FD46  retardo: lo unico visible es $E9 = Y, I = 1 y N/Z segun Y (LDY $E9).
//  $FC00  (solo observa) el SAVE empieza un bloque nuevo.
//  $F81D  (solo observa) el COS vuelve a su prompt: fin de la sesion de cinta.
//
// Sin los retardos un byte tarda ~13.000 instrucciones y el tono de guia de un
// SAVE ~1,4 millones; con ellos, una fraccion.
HookResult System2Machine::trap(uint16_t pc) {
    if (!tape_ || pc < kCosRomBase) return HOOK_NONE;   // sin cinta (o 0x02 en RAM) se ejecuta la instruccion original
    const uint32_t insns = cpu_.instructionCount();
    switch (pc) {
        case kCosPrompt:
            tape_->onPrompt();
            return HOOK_NONE;

        case kCosWriteBlock:
            tape_->markLeader();
            return HOOK_NONE;

        case kCosCarrier:
            switch (tape_->sync(insns)) {
                case SerialTape::SYNC_READY:
                    cpu_.setA(0xFF);
                    cpu_.setPc(kCosCarrierFound);
                    return HOOK_DONE;
                case SerialTape::SYNC_ABORT:
                    cpu_.setPc(kCosCarrierAbort);
                    return HOOK_DONE;
                default:
                    return HOOK_WAIT;
            }

        case kCosGetStart: {
            uint8_t b;
            if (!tape_->getByte(&b, insns)) return HOOK_WAIT;
            cpu_.setA(b);
            cpu_.setX(0);
            cpu_.setPc(kCosGetDone);
            return HOOK_DONE;
        }

        case kCosPutBits: {
            tape_->putByte(cpu_.a(), insns);
            cpu_.setA((uint8_t)((cpu_.a() >> 1) | (cpu_.carry() ? 0x80 : 0)));
            cpu_.setX(0xFF);
            cpu_.setPc(kCosPutDone);
            return HOOK_DONE;
        }

        case kCosDelay: {
            const uint8_t y = cpu_.y();
            write(0x00E9, y);                                   // STY $E9
            uint8_t p = (uint8_t)(cpu_.status() | Cpu6502Defs::FLAG_I);          // SEI
            p = (uint8_t)((p & ~(Cpu6502Defs::FLAG_N | Cpu6502Defs::FLAG_Z)) | (y & 0x80) | (y == 0 ? Cpu6502Defs::FLAG_Z : 0));  // LDY $E9
            cpu_.setStatus(p);
            cpu_.returnFromSubroutine();                        // RTS
            return HOOK_DONE;
        }

        default:
            return HOOK_NONE;
    }
}

void System2Machine::run(uint16_t instructions) {
    // Las rutinas interceptadas estan en la ROM, parcheadas con 0x02 en kCosOps:
    // la CPU llama a trap() solo al llegar a ellas (cpu_.run se detiene si la
    // cinta espera datos y se vuelve al bucle principal).
    const uint16_t done = cpu_.run(instructions);
    pumpKeyboard(done);
}
