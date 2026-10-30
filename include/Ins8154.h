#pragma once

#include <stdint.h>

// INS8154: 128 bytes de RAM + 2 puertos de E/S de 8 bits.
// Mapa tal como lo usa el COS del System 2 (INS8154 "IC8" en 0x0E00):
//   offset 0x00-0x7F  registros de E/S (la ROM usa solo los de abajo)
//     0x00-0x07  bit n del puerto A  (lectura: bit 7 = estado; escritura: borra el bit)
//     0x08-0x0F  bit n del puerto B  (idem)
//     0x10-0x17  escritura: pone a 1 el bit n del puerto A
//     0x18-0x1F  escritura: pone a 1 el bit n del puerto B
//     0x20 puerto A (datos)   0x21 puerto B (datos)
//     0x22 DDR A              0x23 DDR B              0x24 MDR (solo se guarda)
//   offset 0x80-0xFF  RAM interna (128 bytes)
class Ins8154 {
public:
    // Lo que hay conectado a los pines (teclado, cinta...).
    class Pins {
    public:
        virtual uint8_t readA() = 0;           // lectura de datos (con efectos laterales)
        virtual uint8_t readB() = 0;
        virtual uint8_t peekA() = 0;           // estado de los pines sin efectos laterales
        virtual uint8_t peekB() = 0;
        virtual void writeA(uint8_t pins) = 0; // nivel de los pines tras una escritura
        virtual void writeB(uint8_t pins) = 0;
    };

    explicit Ins8154(Pins& pins) : pins_(pins) { reset(true); }

    void reset(bool clearRam);
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    uint8_t ddrA() const { return ddr_a_; }
    uint8_t ddrB() const { return ddr_b_; }
    uint8_t outA() const { return out_a_; }
    uint8_t outB() const { return out_b_; }

private:
    Pins& pins_;
    uint8_t ram_[128];
    uint8_t out_a_, out_b_, ddr_a_, ddr_b_, mdr_;

    void driveA() { pins_.writeA((uint8_t)((out_a_ & ddr_a_) | ~ddr_a_)); }
    void driveB() { pins_.writeB((uint8_t)((out_b_ & ddr_b_) | ~ddr_b_)); }
};
