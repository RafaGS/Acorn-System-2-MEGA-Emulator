#pragma once

#include <stdint.h>

// VIA 6522 de la interfaz de impresora (0x0C00-0x0C0F, repetida). Solo lo que
// usa el COS: registros que se pueden escribir y leer (PCR, DDR...), los
// datos de salida se descartan y los pines de entrada leen 0 (la impresora
// nunca esta ocupada). El COS no activa la impresora hasta que se escribe un
// valor distinto de 0 en bits 1-3 de PCR.
class PrinterPort {
public:
    PrinterPort() { reset(); }
    void reset() { for (uint8_t i = 0; i < 16; i++) regs_[i] = 0; }

    uint8_t read(uint8_t reg) const {
        reg &= 0x0F;
        if (reg == 0x00 || reg == 0x01 || reg == 0x0F) return 0x00;  // pines de entrada
        return regs_[reg];
    }

    void write(uint8_t reg, uint8_t value) {
        reg &= 0x0F;
        if (reg == 0x00 || reg == 0x01 || reg == 0x0F) return;  // datos a la impresora: descartados
        regs_[reg] = value;
    }

private:
    uint8_t regs_[16];
};
