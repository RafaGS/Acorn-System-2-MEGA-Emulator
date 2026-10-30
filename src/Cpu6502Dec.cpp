#include "Cpu6502.h"

Cpu6502Defs::DecResult Cpu6502Defs::adcDecimal(uint8_t a, uint8_t m, uint8_t carry_in) {
    const uint16_t bin = (uint16_t)a + m + carry_in;
    // Decimal NMOS: Z sale del resultado binario; N y V del resultado
    // intermedio (antes de corregir el nibble alto).
    uint8_t lo = (uint8_t)((a & 0x0F) + (m & 0x0F) + carry_in);
    if (lo >= 0x0A) lo = (uint8_t)(((lo + 0x06) & 0x0F) + 0x10);
    uint16_t sum = (uint16_t)((a & 0xF0) + (m & 0xF0) + lo);
    DecResult r;
    r.z = (uint8_t)bin;
    r.n = (uint8_t)sum;
    r.v = ((~(a ^ m) & (a ^ sum) & 0x80) != 0) ? 1 : 0;
    if (sum >= 0xA0) sum += 0x60;
    r.c = (sum >= 0x100) ? 1 : 0;
    r.a = (uint8_t)sum;
    return r;
}

uint8_t Cpu6502Defs::sbcDecimal(uint8_t a, uint8_t m, uint8_t carry_in) {
    int16_t lo = (int16_t)(a & 0x0F) - (int16_t)(m & 0x0F) + carry_in - 1;
    if (lo < 0) lo = (int16_t)(((lo - 0x06) & 0x0F) - 0x10);
    int16_t r = (int16_t)(a & 0xF0) - (int16_t)(m & 0xF0) + lo;
    if (r < 0) r -= 0x60;
    return (uint8_t)r;
}

