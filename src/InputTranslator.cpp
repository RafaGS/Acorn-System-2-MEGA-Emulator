#include "InputTranslator.h"

void InputTranslator::feed(uint8_t b, uint32_t nowMs) {
    switch (state_) {
        case NORMAL:
            if (b == 0x1B) {
                state_ = ESC;
                esc_ms_ = nowMs;
                return;
            }
            normal(b);
            return;

        case ESC:
            if (b == '[' || b == 'O') {
                state_ = CSI;
                return;
            }
            state_ = NORMAL;
            sink_.key(0x1B);
            feed(b, nowMs);
            return;

        case CSI:
            if (b >= '@' && b <= '~') {
                state_ = NORMAL;
                switch (b) {
                    case 'A': sink_.key(0x17); break;  // arriba  (^W)
                    case 'B': sink_.key(0x1A); break;  // abajo   (^Z)
                    case 'C': sink_.key(0x13); break;  // derecha (^S)
                    case 'D': sink_.key(0x01); break;  // izquierda (^A)
                    default: break;
                }
            }
            return;
    }
}

void InputTranslator::idle(uint32_t nowMs) {
    if (state_ == ESC && (uint32_t)(nowMs - esc_ms_) >= 60) {
        state_ = NORMAL;
        sink_.key(0x1B);
    }
}

void InputTranslator::normal(uint8_t b) {
    const bool was_cr = last_cr_;
    last_cr_ = false;

    switch (b) {
        case 0x1C: sink_.command(CMD_RESET); return;
        case 0x1D: sink_.command(CMD_REDRAW); return;
        case 0x18: sink_.command(CMD_MONITOR); return;  // Ctrl-X
        case 0x1E: fold_upper_ = !fold_upper_; return;
        case 0x0D: last_cr_ = true; sink_.key(0x0D); return;
        case 0x0A: if (!was_cr) sink_.key(0x0D); return;
        case 0x08:
        case 0x7F: sink_.key(0x7F); return;
        default: break;
    }

    if (b & 0x80) return;  // el teclado es ASCII de 7 bits
    if (fold_upper_ && b >= 'a' && b <= 'z') b = (uint8_t)(b - 0x20);
    sink_.key(b);
}
