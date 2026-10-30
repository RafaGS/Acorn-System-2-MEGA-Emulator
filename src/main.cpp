// Acorn System 2 en Arduino Mega 2560. Consola exclusivamente por puerto serie:
// la pantalla (RAM de video teletexto) se dibuja con secuencias ANSI y el
// teclado es lo que se teclee en el terminal. La logica esta en Console (que
// tambien se prueba en el host); aqui solo se conecta al Serial de Arduino.
#include <Arduino.h>

#include "Console.h"

class ArduinoSerialPort : public SerialPort {
public:
    int available() override { return Serial.available(); }
    int read() override { return Serial.read(); }
    void put(uint8_t c) override { Serial.write(c); }
};

static ArduinoSerialPort port;
static System2Machine machine;
static Console console(machine, port);

void setup() {
    Serial.begin(SYS2_BAUD);
    console.begin(millis());
}

void loop() {
    console.poll(millis());
}
