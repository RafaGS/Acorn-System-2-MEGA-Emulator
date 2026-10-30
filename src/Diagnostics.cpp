#include "Diagnostics.h"

#include <stddef.h>

#ifdef __AVR__
#include <avr/io.h>

extern uint8_t _end;          // fin de .bss (inicio del heap)
extern uint8_t __stack;       // RAMEND (lo define el enlazador)
extern int __heap_start, *__brkval;

static const uint8_t kCanary = 0xC5;

int16_t Diagnostics::freeRam() {
    uint8_t marker;
    const uint8_t* heapEnd = __brkval == 0 ? (const uint8_t*)&__heap_start : (const uint8_t*)__brkval;
    return (int16_t)(&marker - heapEnd);
}

int16_t Diagnostics::minFreeRam() {
    const uint8_t* p = &_end;
    const uint8_t* top = &__stack;
    uint16_t n = 0;
    while (p <= top && *p == kCanary) { p++; n++; }
    return (int16_t)n;
}

void Diagnostics::resetMin() {
    // Pinta de 0xC5 la SRAM libre, solo por debajo del puntero de pila actual
    // (menos un margen): lo que hay por encima esta en uso.
    uint8_t* p = &_end;
    const uint8_t* limit = (const uint8_t*)SP - 16;
    while (p < limit) *p++ = kCanary;
}
#else
int16_t Diagnostics::freeRam() { return -1; }
int16_t Diagnostics::minFreeRam() { return -1; }
void Diagnostics::resetMin() {}
#endif

void Diagnostics::begin(uint32_t nowMs, uint32_t insns, uint32_t cycles) {
    resetMin();   // el "minimo de SRAM libre" se cuenta desde aqui (el arranque de Arduino apenas usa pila)
    last_ms_ = nowMs;
    last_insns_ = insns;
    last_cycles_ = cycles;
    have_ = false;
}

// delta * 1000 / dt sin desbordar 32 bits (delta < 2^32, dt >= 1000).
static uint32_t perSecond(uint32_t delta, uint32_t dtMs) {
    return (delta / dtMs) * 1000UL + ((delta % dtMs) * 1000UL) / dtMs;
}

void Diagnostics::sample(uint32_t nowMs, uint32_t insns, uint32_t cycles) {
    const uint32_t dt = (uint32_t)(nowMs - last_ms_);
    if (dt < 1000) return;
    // Las restas valen aunque los contadores de 32 bits den la vuelta.
    ips_ = perSecond((uint32_t)(insns - last_insns_), dt);
    cps_ = perSecond((uint32_t)(cycles - last_cycles_), dt);
    have_ = true;
    last_ms_ = nowMs;
    last_insns_ = insns;
    last_cycles_ = cycles;
}

namespace {
struct Out {
    char* p;
    char* end;
    void ch(char c) { if (p < end) *p++ = c; }
    void str(const char* s) { while (*s) ch(*s++); }
    void num(uint32_t v) {
        char tmp[10];
        uint8_t n = 0;
        do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) ch(tmp[--n]);
    }
    // v con un decimal fijo: 1234 -> "123.4"
    void num1(uint32_t tenths) { num(tenths / 10); ch('.'); num(tenths % 10); }
};
}  // namespace

uint8_t Diagnostics::formatValues(char* buf, uint8_t size, bool have, uint32_t ips, uint32_t cps,
                                  int16_t freeNow, int16_t freeMin) {
    Out o = {buf, buf + size - 1};

    o.str("6502: ");
    if (have) {
        const uint32_t hundredths = cps / 10000UL;   // centesimas de MHz = % de 1 MHz
        o.num(hundredths / 100);
        o.ch('.');
        if (hundredths % 100 < 10) o.ch('0');
        o.num(hundredths % 100);
        o.str(" MHz (");
        o.num(hundredths);
        o.str("% de 1 MHz), ");
        o.num1((ips + 50) / 100);                     // instr/s en miles, un decimal
        o.str("k instr/s");
    } else {
        o.str("midiendo...");
    }

    o.str(" | SRAM libre ");
    if (freeNow >= 0) {
        o.num((uint32_t)freeNow);
        o.str(" B");
        if (freeMin >= 0) {
            o.str(" (min ");
            o.num((uint32_t)freeMin);
            o.str(" B)");
        }
    } else {
        o.str("n/d");
    }

    *o.p = 0;
    return (uint8_t)(o.p - buf);
}

uint8_t Diagnostics::format(char* buf, uint8_t size) const {
    return formatValues(buf, size, have_, ips_, cps_, freeRam(), minFreeRam());
}
