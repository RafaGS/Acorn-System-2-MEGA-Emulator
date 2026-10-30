#pragma once

#include "config.h"

// ROM del sistema operativo COS del Acorn System 2 (IC7, 2 KB, mapeada en
// 0xF800-0xFFFF). Datos en src/cos_rom.cpp (generado por tools/gen_rom.py).
static const uint16_t kCosRomBase = 0xF800;
static const uint16_t kCosRomSize = 2048;

extern const uint8_t kCosRom[kCosRomSize] PROGMEM;

// Copia de la ROM con el primer byte de cada rutina interceptada (ver
// System2Machine::trap) sustituido por 0x02: la CPU solo la usa para leer
// codigos de operacion, asi que los ganchos no cuestan nada fuera de ellos.
extern const uint8_t kCosOps[kCosRomSize] PROGMEM;
