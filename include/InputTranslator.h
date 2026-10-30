#pragma once

#include <stdint.h>

#include "config.h"

// Traduce los bytes que llegan por el puerto serie (terminal) a teclas del
// teclado ASCII del System 2 y a comandos del emulador:
//   - CR / LF / CRLF -> 0x0D (Return)
//   - 0x08 / 0x7F    -> 0x7F (borrar), como hace MAME
//   - flechas ANSI (ESC [ A/B/C/D) -> ^W ^Z ^S ^A: el cursor del COS
//   - Ctrl-\ = RESET, Ctrl-] = redibujar pantalla, Ctrl-^ = alternar forzado a mayusculas,
//     Ctrl-X = informe de velocidad y memoria en la linea de estado (on/off)
class InputTranslator {
public:
    enum Command { CMD_RESET, CMD_REDRAW, CMD_MONITOR };

    class Sink {
    public:
        virtual void key(uint8_t code) = 0;
        virtual void command(Command cmd) = 0;
    };

    explicit InputTranslator(Sink& sink)
        : sink_(sink), state_(NORMAL), esc_ms_(0), last_cr_(false), fold_upper_(SYS2_UPPERCASE_INPUT != 0) {}

    // Cada llamada a feed() puede producir hasta 2 teclas.
    void feed(uint8_t byte, uint32_t nowMs);
    // Llamar periodicamente: un ESC suelto se entrega como tecla ESC pasados 60 ms.
    void idle(uint32_t nowMs);

    bool foldUpper() const { return fold_upper_; }

private:
    enum State { NORMAL, ESC, CSI };

    void normal(uint8_t byte);

    Sink& sink_;
    State state_;
    uint32_t esc_ms_;
    bool last_cr_;
    bool fold_upper_;
};
