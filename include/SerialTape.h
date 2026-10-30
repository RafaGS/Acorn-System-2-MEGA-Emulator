#pragma once

#include <stdint.h>

#include "Io.h"

// Cassette virtual por el puerto serie.
//
// El COS lee y graba la cinta byte a byte con OSBGET ($FCA6) y OSBPUT ($FD23),
// que mueven los bits por PA6/PA7 del INS8154 con retardos de 300 baudios.
// System2Machine::run() intercepta esas rutinas (y el detector de portadora
// $FC4B y los retardos $FD46) y las conecta aqui: la cinta es el mismo flujo
// de bytes que el COS grabaria, dividido en bloques (cada bloque empieza donde
// el COS genera tono de guia).
//
// Mientras hay una "sesion de cinta" el emulador deja de dibujar la pantalla y
// habla con el PC con un protocolo de lineas de texto ASCII (terminadas en
// CR y/o LF), pensado para tools/sys2term.py pero legible por una persona:
//
//   Mega -> PC (SAVE, "#TAPE OUT"):
//       #TAPE OUT        empieza la sesion
//       L                empieza un bloque nuevo (tono de guia)
//       D<hex><ck>       hasta 16 bytes en hexadecimal + suma de comprobacion (2 hex)
//       #END             fin de la sesion (despues se redibuja la pantalla)
//   Mega -> PC (LOAD/CAT/FLOAD, "#TAPE IN"):
//       #TAPE IN         empieza la sesion
//       #SYNC            "dame el siguiente bloque" (el COS busca tono de guia)
//       #NEED            "dame mas bytes del bloque actual"
//       #EOF             la cinta se acabo en mitad de un bloque
//       #ERR             linea recibida con error: reenviala
//       #END
//   PC -> Mega:
//       L                el siguiente D es el principio de un bloque
//       D<hex><ck>       hasta 16 bytes (un D por cada #SYNC / #NEED)
//       E                fin de cinta: #SYNC pasa a abortar la operacion, como pulsar ESPACIO
//       R                rebobinar: tira lo que hubiera en el buffer
//       ESPACIO al principio de una linea: cancelar (igual que en el COS real)
//
// Fin de sesion: el COS vuelve a su prompt ($F81D) o pasan mas de kIdleInsns
// instrucciones sin tocar la cinta.
class SerialTape {
public:
    enum SyncResult { SYNC_READY, SYNC_ABORT, SYNC_WAIT };

    static const uint8_t kChunk = 16;               // bytes por linea D
    static const uint32_t kIdleInsns = 500000UL;    // fin de sesion por inactividad

    explicit SerialTape(ByteSink& out);

    // ---- lado CPU (llamados desde System2Machine::run) -------------------
    SyncResult sync(uint32_t insns);                // detector de portadora ($FC4B)
    bool getByte(uint8_t* b, uint32_t insns);       // false = aun no hay byte: la CPU espera
    void putByte(uint8_t b, uint32_t insns);
    void markLeader() { leader_pending_ = true; }   // el COS genera tono de guia
    void onPrompt();                                // el COS volvio a su prompt

    // ---- lado consola ----------------------------------------------------
    bool active() const { return mode_ != MODE_IDLE; }
    void feed(uint8_t c);                           // byte recibido del PC durante una sesion
    void service(uint32_t insns);                   // fin de sesion por inactividad
    bool takeEnded() { const bool e = ended_; ended_ = false; return e; }
    void reset();                                   // RESET de la maquina

private:
    enum Mode { MODE_IDLE, MODE_IN, MODE_OUT };
    enum Request { REQ_NONE, REQ_SYNC, REQ_NEED };

    static const uint8_t kLineMax = 2 * kChunk + 8;

    ByteSink& out_;
    Mode mode_;
    Request awaiting_;
    bool ended_;

    uint8_t fifo_[kChunk];
    uint8_t head_, count_;
    bool lead_, lead_next_, eof_, eof_reported_, abort_;

    uint8_t out_buf_[kChunk];
    uint8_t out_n_;
    bool leader_pending_;

    uint32_t last_op_;

    char line_[kLineMax];
    uint8_t line_n_;
    bool overflow_;

    void str(const char* s);
    void hex2(uint8_t v);
    void startSession(Mode m);
    void endSession();
    void ensure(Mode m, uint32_t insns);
    void request(Request r);
    void flushLine();
    void processLine();
    void parseData();
    void error();
};
