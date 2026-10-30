#pragma once

#include <stdint.h>

// MC6845 en 0x0800 (registro de direccion) / 0x0801 (datos), repetido cada 2
// bytes. R0-R11 son de solo escritura; R12-R15 (inicio de pantalla y cursor)
// se pueden leer. El COS lo comprueba escribiendo 0xAA en R13 y leyendolo
// (rutina de reset del COS, cos.ic7 en 0xFF0A-0xFF19) para elegir la tabla de parametros.
class Crtc6845 {
public:
    Crtc6845() { reset(); }

    void reset() {
        for (uint8_t i = 0; i < 18; i++) regs_[i] = 0;
        addr_ = 0;
    }

    uint8_t read(uint8_t rs) const {
        if (rs & 1) {
            if (addr_ >= 12 && addr_ <= 17) return regs_[addr_];
        }
        return 0;
    }

    void write(uint8_t rs, uint8_t value) {
        if (!(rs & 1)) {
            addr_ = (uint8_t)(value & 0x1F);
            return;
        }
        if (addr_ < 18) regs_[addr_] = value;
    }

    uint8_t reg(uint8_t n) const { return n < 18 ? regs_[n] : 0; }

    // Direccion de memoria de caracter donde empieza la pantalla (R12:R13) y
    // posicion del cursor (R14:R15). La RAM de video es de 1 KB: los bits
    // altos no llegan a la memoria.
    uint16_t startAddress() const { return (uint16_t)(((regs_[12] & 0x3F) << 8) | regs_[13]); }
    uint16_t cursorAddress() const { return (uint16_t)(((regs_[14] & 0x3F) << 8) | regs_[15]); }
    uint8_t charsPerRow() const { return regs_[1]; }      // R1: caracteres visibles por fila
    uint8_t rowsDisplayed() const { return regs_[6]; }    // R6: filas de caracteres visibles

private:
    uint8_t regs_[18];
    uint8_t addr_;
};
