#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Portabilidad AVR / host (los tests nativos compilan el mismo codigo).
// ---------------------------------------------------------------------------
#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#endif

// ---------------------------------------------------------------------------
// Opciones de compilacion (se pueden sobreescribir con -D en platformio.ini).
// ---------------------------------------------------------------------------

// Velocidad del puerto serie.
#ifndef SYS2_BAUD
#define SYS2_BAUD 115200
#endif

// RAM de expansion en 0x2000 (tarjeta de RAM del bus; el System 2 real admite
// 4/8 KB ahi). Solo se mapea la parte que se indique. El Mega tiene 8 KB de
// SRAM y con 2 KB quedan unos 2 KB libres para la pila; mas de 2 KB no cabe
// (en el host de pruebas se admite hasta 8).
#ifndef SYS2_EXT_RAM_KB
#define SYS2_EXT_RAM_KB 2
#endif
#if defined(__AVR__) && SYS2_EXT_RAM_KB > 2
#error "SYS2_EXT_RAM_KB > 2 no cabe en los 8 KB de SRAM del Mega"
#endif
#if SYS2_EXT_RAM_KB < 0 || SYS2_EXT_RAM_KB > 8
#error "SYS2_EXT_RAM_KB debe estar entre 0 y 8"
#endif

// Milisegundos minimos entre refrescos de pantalla por el puerto serie.
#ifndef SYS2_REFRESH_MS
#define SYS2_REFRESH_MS 50
#endif

// Instrucciones del 6502 ejecutadas entre dos pasadas del bucle principal
// (entrada serie, refresco de pantalla).
#ifndef SYS2_SLICE_INSNS
#define SYS2_SLICE_INSNS 200
#endif

// 1 = convertir minusculas a mayusculas en la entrada (los comandos del COS
// son en mayusculas). Se puede alternar en caliente con Ctrl-^.
#ifndef SYS2_UPPERCASE_INPUT
#define SYS2_UPPERCASE_INPUT 1
#endif

// Mosaicos del teletexto: 0 = ASCII ('#'), 1 = sextantes Unicode
// (U+1FB00..; requiere una fuente de terminal que los incluya).
#ifndef SYS2_MOSAIC_STYLE
#define SYS2_MOSAIC_STYLE 1
#endif

// 1 = no emitir UTF-8 en el texto alfanumerico (libra, flechas, fracciones...).
#ifndef SYS2_ASCII_ONLY
#define SYS2_ASCII_ONLY 0
#endif

// Instrucciones que debe esperar el teclado entre dos teclas consecutivas
// tras consumir el strobe (evita que el COS pierda teclas, ver
// System2Machine::pumpKeyboard()).
#ifndef SYS2_KBD_GAP_INSNS
#define SYS2_KBD_GAP_INSNS 2000
#endif
