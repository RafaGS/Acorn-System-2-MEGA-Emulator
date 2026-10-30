#pragma once

#include <stdint.h>

// Destino de bytes (el puerto serie en el Mega, un buffer en los tests).
class ByteSink {
public:
    virtual void put(uint8_t c) = 0;
};

// Puerto serie visto por la consola: entrada no bloqueante + salida.
class SerialPort : public ByteSink {
public:
    virtual int available() = 0;  // bytes pendientes de leer
    virtual int read() = 0;       // siguiente byte (solo si available() > 0)
};
