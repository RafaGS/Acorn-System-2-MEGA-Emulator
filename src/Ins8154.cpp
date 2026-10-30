#include "Ins8154.h"

#include <string.h>

void Ins8154::reset(bool clearRam) {
    if (clearRam) memset(ram_, 0, sizeof(ram_));
    out_a_ = out_b_ = ddr_a_ = ddr_b_ = mdr_ = 0;
}

uint8_t Ins8154::read(uint8_t offset) {
    if (offset & 0x80) return ram_[offset & 0x7F];

    switch (offset) {
        case 0x20: return pins_.readA();
        case 0x21: return pins_.readB();
        default: break;
    }

    if (offset < 0x20) {
        // Lectura de un bit suelto: 0x80 si el pin esta a 1 y es entrada.
        const uint8_t bit = (uint8_t)(1u << (offset & 7));
        const bool portB = (offset & 0x08) != 0;
        const uint8_t pin = portB ? pins_.peekB() : pins_.peekA();
        const uint8_t ddr = portB ? ddr_b_ : ddr_a_;
        return (pin & ~ddr & bit) ? 0x80 : 0x00;
    }
    return 0xFF;  // DDR/MDR no se pueden leer; resto sin decodificar
}

void Ins8154::write(uint8_t offset, uint8_t value) {
    if (offset & 0x80) {
        ram_[offset & 0x7F] = value;
        return;
    }

    if (offset < 0x20) {
        const uint8_t bit = (uint8_t)(1u << (offset & 7));
        const bool portB = (offset & 0x08) != 0;
        const bool set = (offset & 0x10) != 0;
        uint8_t& out = portB ? out_b_ : out_a_;
        out = set ? (uint8_t)(out | bit) : (uint8_t)(out & ~bit);
        if (portB) driveB(); else driveA();
        return;
    }

    switch (offset) {
        case 0x20: out_a_ = value; driveA(); break;
        case 0x21: out_b_ = value; driveB(); break;
        case 0x22: ddr_a_ = value; driveA(); break;
        case 0x23: ddr_b_ = value; driveB(); break;
        case 0x24: mdr_ = value; break;
        default: break;
    }
}
