#pragma once

#include <stdint.h>

#include "Io.h"
#include "System2Machine.h"

// Dibuja la pantalla del System 2 (RAM de video interpretada como teletexto,
// lo que haria el SAA5050) en un terminal ANSI/VT100 por el puerto serie.
// Solo reenvia las filas que han cambiado desde el ultimo refresco.
//
// Teletexto implementado: texto con 7 colores, mosaicos (contiguos; los
// "separados" se dibujan igual), fondo negro/nuevo fondo, ocultar, mantener
// grafico. No implementado: parpadeo y doble altura (se dibujan normales).
class TeletextTerminal {
public:
    static const uint8_t kMaxCols = 40;
    static const uint8_t kMaxRows = 25;

    TeletextTerminal();

    void begin(ByteSink& out) { out_ = &out; }
    void redraw(const System2Machine& machine);  // limpia el terminal y lo dibuja entero
    void update(const System2Machine& machine);  // solo filas cambiadas + cursor
    // Linea de estado (debajo de la pantalla): texto propio o la ayuda de teclas.
    void setStatus(const System2Machine& machine, const char* text);
    void showHelp(const System2Machine& machine);

private:
    struct Attr {
        uint8_t fg, bg;
        bool gfx, sep, conceal, hold;
        uint8_t held;  // patron de mosaico retenido (6 bits)
    };

    ByteSink* out_;
    uint8_t shadow_[kMaxRows][kMaxCols];
    bool shadow_valid_;
    uint8_t cursor_row_, cursor_col_;
    bool cursor_visible_, cursor_known_;
    uint8_t cur_fg_, cur_bg_;

    void put(uint8_t c) { if (out_) out_->put(c); }
    void puts_p(const char* progmemStr);
    void putStr(const char* s);
    void putNumber(uint8_t n);
    void putUtf8(uint32_t cp);
    void moveTo(uint8_t row1, uint8_t col1);
    void setColors(uint8_t fg, uint8_t bg);
    void drawMosaic(uint8_t pattern);
    void drawAlpha(uint8_t code);
    void renderRow(uint8_t row, const uint8_t* cells, uint8_t cols);
    void statusRow(const System2Machine& machine, const char* text, bool fromProgmem);
};
