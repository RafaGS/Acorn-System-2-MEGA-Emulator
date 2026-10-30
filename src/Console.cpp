#include "Console.h"

Console::Console(System2Machine& machine, SerialPort& port)
    : machine_(machine), port_(port), input_(*this), tape_(port),
      last_refresh_(0), last_monitor_(0), monitor_(false), monitor_due_(false) {
    terminal_.begin(port);
    machine_.attachTape(&tape_);
}

void Console::begin(uint32_t nowMs) {
    machine_.reset(true);
    tape_.reset();
    tape_.takeEnded();
    diag_.begin(nowMs, machine_.cpu().instructionCount(), machine_.cpu().cycles());
    last_refresh_ = nowMs;
    redraw();
}

void Console::redraw() {
    terminal_.redraw(machine_);
    if (monitor_) monitor_due_ = true;
}

void Console::command(InputTranslator::Command cmd) {
    switch (cmd) {
        case InputTranslator::CMD_RESET:
            machine_.reset(false);  // boton RESET: solo la CPU
            tape_.reset();
            tape_.takeEnded();
            redraw();
            break;
        case InputTranslator::CMD_REDRAW:
            redraw();
            break;
        case InputTranslator::CMD_MONITOR:
            monitor_ = !monitor_;
            if (monitor_) {
                monitor_due_ = true;
            } else {
                terminal_.showHelp(machine_);
                terminal_.update(machine_);   // devuelve el cursor a su sitio
            }
            break;
    }
}

void Console::showMonitor() {
    char text[80];
    diag_.format(text, sizeof(text));
    terminal_.setStatus(machine_, text);
    terminal_.update(machine_);  // devuelve el cursor a su sitio
}

void Console::readInput(uint32_t nowMs) {
    while (port_.available() > 0) {
        if (tape_.active()) {
            // Durante una sesion de cinta todo lo que llega es protocolo, salvo RESET.
            const uint8_t b = (uint8_t)port_.read();
            if (b == 0x1C) command(InputTranslator::CMD_RESET);
            else tape_.feed(b);
        } else {
            if (machine_.keyQueueFree() < 2) break;  // el byte espera en el buffer serie
            input_.feed((uint8_t)port_.read(), nowMs);
        }
    }
}

void Console::poll(uint32_t nowMs) {
    machine_.run(SYS2_SLICE_INSNS);
    const uint32_t insns = machine_.cpu().instructionCount();

    tape_.service(insns);
    if (tape_.takeEnded()) {
        redraw();
        last_refresh_ = nowMs;
    }

    readInput(nowMs);
    input_.idle(nowMs);
    diag_.sample(nowMs, insns, machine_.cpu().cycles());

    if (tape_.active()) return;  // sesion de cinta: la pantalla no se toca

    if ((uint32_t)(nowMs - last_refresh_) >= SYS2_REFRESH_MS) {
        last_refresh_ = nowMs;
        terminal_.update(machine_);
    }
    if (monitor_ && (monitor_due_ || (uint32_t)(nowMs - last_monitor_) >= 1000)) {
        monitor_due_ = false;
        last_monitor_ = nowMs;
        showMonitor();
    }
}
