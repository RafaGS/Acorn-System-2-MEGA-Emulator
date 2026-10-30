#pragma once

#include <stdint.h>

// Medidas de rendimiento y memoria para el comando Ctrl-X.
//
//  - Velocidad: ciclos de reloj de un 6502 real que el emulador ejecuta por
//    segundo (Cpu6502::cycles(): tabla de tiempos NMOS + cruces de pagina +
//    saltos), promediados en ventanas de ~1 s. 1.000.000 ciclos/s = un System 2
//    a su 1 MHz. Incluye el tiempo que el Mega dedica a dibujar la pantalla por
//    el puerto serie y a esperar datos de la cinta, que es lo que ve el usuario.
//  - SRAM libre ahora = distancia entre el final del heap y el puntero de pila.
//  - Minimo de SRAM libre = "high-water mark" real de la pila desde el arranque:
//    en begin() se pinta toda la SRAM libre con 0xC5 y se cuentan los bytes que
//    siguen intactos (tecnica de la FAQ de avr-libc, "How much stack space does
//    my program need?").
//
// En el host (tests) las medidas de memoria devuelven -1 ("no disponible").
class Diagnostics {
public:
    Diagnostics() : last_ms_(0), last_insns_(0), last_cycles_(0), ips_(0), cps_(0), have_(false) {}

    void begin(uint32_t nowMs, uint32_t insns, uint32_t cycles);   // tambien repinta la SRAM libre (resetMin)
    // Llamar en cada vuelta del bucle principal; recalcula cada >= 1000 ms.
    void sample(uint32_t nowMs, uint32_t insns, uint32_t cycles);

    bool haveRates() const { return have_; }
    uint32_t instructionsPerSecond() const { return ips_; }
    uint32_t cyclesPerSecond() const { return cps_; }

    static int16_t freeRam();      // SRAM libre ahora (-1 si no se puede medir)
    static int16_t minFreeRam();   // minimo desde el arranque o desde resetMin() (-1 si no se puede medir)
    static void resetMin();        // vuelve a pintar la zona libre: el minimo se recalcula desde ahora

    // "6502: 0.41 MHz (41% de 1 MHz), 118.3k instr/s | SRAM libre 2104 B (min 1980 B)".
    // Devuelve la longitud escrita (siempre termina en 0).
    uint8_t format(char* buf, uint8_t size) const;

    // Mismo formato con valores dados, para poder probarlo sin hardware.
    static uint8_t formatValues(char* buf, uint8_t size, bool have, uint32_t ips, uint32_t cps,
                                int16_t freeNow, int16_t freeMin);

private:
    uint32_t last_ms_, last_insns_, last_cycles_;
    uint32_t ips_, cps_;
    bool have_;
};
