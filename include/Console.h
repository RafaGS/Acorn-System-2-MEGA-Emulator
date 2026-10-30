#pragma once

#include <stdint.h>

#include "Diagnostics.h"
#include "InputTranslator.h"
#include "Io.h"
#include "SerialTape.h"
#include "System2Machine.h"
#include "TeletextTerminal.h"

// Consola del System 2 por puerto serie: ejecuta la maquina, dibuja la
// pantalla, traduce el teclado, atiende el diagnostico (Ctrl-X) y la cinta
// virtual. main.cpp solo aporta el SerialPort real; los tests, uno en memoria.
class Console : public InputTranslator::Sink {
public:
    Console(System2Machine& machine, SerialPort& port);

    void begin(uint32_t nowMs);   // arranque en frio + primer dibujo
    void poll(uint32_t nowMs);    // una vuelta del bucle principal

    // InputTranslator::Sink
    void key(uint8_t code) override { machine_.queueKey(code); }
    void command(InputTranslator::Command cmd) override;

    System2Machine& machine() { return machine_; }
    const SerialTape& tape() const { return tape_; }

private:
    void readInput(uint32_t nowMs);
    void redraw();
    void showMonitor();

    System2Machine& machine_;
    SerialPort& port_;
    TeletextTerminal terminal_;
    InputTranslator input_;
    SerialTape tape_;
    Diagnostics diag_;

    uint32_t last_refresh_;
    uint32_t last_monitor_;
    bool monitor_;
    bool monitor_due_;
};
