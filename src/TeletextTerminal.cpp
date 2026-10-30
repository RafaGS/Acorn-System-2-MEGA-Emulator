#include "TeletextTerminal.h"

#include <string.h>

static const char kStatusText[] PROGMEM =
    "Acorn System 2 | Ctrl-\\ RESET  Ctrl-] redibujar  Ctrl-^ mayusc.  Ctrl-X medidor";

TeletextTerminal::TeletextTerminal()
    : out_(0), shadow_valid_(false), cursor_row_(0), cursor_col_(0),
      cursor_visible_(true), cursor_known_(false), cur_fg_(255), cur_bg_(255) {
    memset(shadow_, 0, sizeof(shadow_));
}

void TeletextTerminal::puts_p(const char* s) {
    for (;;) {
        const uint8_t c = pgm_read_byte(s++);
        if (!c) break;
        put(c);
    }
}

void TeletextTerminal::putStr(const char* s) {
    while (*s) put((uint8_t)*s++);
}

void TeletextTerminal::putNumber(uint8_t n) {
    if (n >= 100) put((uint8_t)('0' + n / 100));
    if (n >= 10) put((uint8_t)('0' + (n / 10) % 10));
    put((uint8_t)('0' + n % 10));
}

void TeletextTerminal::putUtf8(uint32_t cp) {
    if (cp < 0x80) {
        put((uint8_t)cp);
    } else if (cp < 0x800) {
        put((uint8_t)(0xC0 | (cp >> 6)));
        put((uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        put((uint8_t)(0xE0 | (cp >> 12)));
        put((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        put((uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        put((uint8_t)(0xF0 | (cp >> 18)));
        put((uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        put((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        put((uint8_t)(0x80 | (cp & 0x3F)));
    }
}

void TeletextTerminal::moveTo(uint8_t row1, uint8_t col1) {
    putStr("\x1b[");
    putNumber(row1);
    put(';');
    putNumber(col1);
    put('H');
}

void TeletextTerminal::setColors(uint8_t fg, uint8_t bg) {
    if (fg == cur_fg_ && bg == cur_bg_) return;
    putStr("\x1b[3");
    put((uint8_t)('0' + fg));
    putStr(";4");
    put((uint8_t)('0' + bg));
    put('m');
    cur_fg_ = fg;
    cur_bg_ = bg;
}

// Mosaico de 2x3 "pixeles". Bits del patron: 0 sup-izq, 1 sup-der, 2 med-izq,
// 3 med-der, 4 inf-izq, 5 inf-der (los codigos teletexto 0x20-0x3F / 0x60-0x7F
// llevan los 5 primeros en los bits 0-4 y el sexto en el bit 6).
void TeletextTerminal::drawMosaic(uint8_t p) {
    p &= 0x3F;
#if SYS2_MOSAIC_STYLE == 1
    if (p == 0) put(' ');
    else if (p == 63) putUtf8(0x2588);   // bloque completo
    else if (p == 21) putUtf8(0x258C);   // mitad izquierda
    else if (p == 42) putUtf8(0x2590);   // mitad derecha
    else putUtf8(0x1FB00u + (uint32_t)(p - 1 - (p > 21 ? 1 : 0) - (p > 42 ? 1 : 0)));  // sextantes
#else
    put(p ? '#' : ' ');
#endif
}

// Juego de caracteres del SAA5050 (version UK). Las diferencias con ASCII se
// han comprobado contra roms/saa5050.bin (ver tools/check_saa5050.py):
// 0x23 libra, 0x5B-0x5E flecha izq/1-2/flecha der/flecha arriba, 0x5F '#',
// 0x60 raya horizontal, 0x7B-0x7F 1/4, doble barra, 3/4, division, bloque.
void TeletextTerminal::drawAlpha(uint8_t code) {
#if SYS2_ASCII_ONLY
    switch (code) {
        case 0x23: case 0x5F: case 0x7F: put('#'); return;
        case 0x5B: put('<'); return;
        case 0x5D: put('>'); return;
        case 0x5E: put('^'); return;
        case 0x5C: case 0x7B: case 0x7D: put('%'); return;
        case 0x60: put('-'); return;
        case 0x7C: put('|'); return;
        case 0x7E: put('/'); return;
        default: put(code); return;
    }
#else
    switch (code) {
        case 0x23: putUtf8(0x00A3); return;  // libra
        case 0x5B: putUtf8(0x2190); return;
        case 0x5C: putUtf8(0x00BD); return;
        case 0x5D: putUtf8(0x2192); return;
        case 0x5E: putUtf8(0x2191); return;
        case 0x5F: put('#'); return;
        case 0x60: putUtf8(0x2500); return;
        case 0x7B: putUtf8(0x00BC); return;
        case 0x7C: putUtf8(0x2016); return;
        case 0x7D: putUtf8(0x00BE); return;
        case 0x7E: putUtf8(0x00F7); return;
        case 0x7F: putUtf8(0x2588); return;
        default: put(code); return;
    }
#endif
}

void TeletextTerminal::renderRow(uint8_t row, const uint8_t* cells, uint8_t cols) {
    moveTo((uint8_t)(row + 1), 1);

    Attr a;
    a.fg = 7; a.bg = 0;
    a.gfx = a.sep = a.conceal = a.hold = false;
    a.held = 0;

    for (uint8_t c = 0; c < cols; c++) {
        const uint8_t code = cells[c] & 0x7F;

        if (code < 0x20) {
            // Codigos de control: se muestran como espacio (o como el mosaico
            // retenido). "Set-at" actuan en esta misma celda; "set-after" en las siguientes.
            switch (code) {
                case 0x18: a.conceal = true; break;
                case 0x19: a.sep = false; break;
                case 0x1A: a.sep = true; break;
                case 0x1C: a.bg = 0; break;
                case 0x1D: a.bg = a.fg; break;
                case 0x1E: a.hold = true; break;
                default: break;
            }
            setColors(a.fg, a.bg);
            if (a.hold && a.gfx && !a.conceal) drawMosaic(a.held); else put(' ');

            if (code >= 0x01 && code <= 0x07) {
                a.gfx = false; a.fg = code; a.conceal = false; a.held = 0;
            } else if (code >= 0x11 && code <= 0x17) {
                a.gfx = true; a.fg = (uint8_t)(code & 7); a.conceal = false;
            } else if (code == 0x1F) {
                a.hold = false;
            }
            continue;
        }

        setColors(a.fg, a.bg);
        if (a.conceal) {
            put(' ');
        } else if (a.gfx && (code & 0x20)) {
            // En modo grafico solo 0x40-0x5F siguen siendo letras.
            const uint8_t pattern = (uint8_t)((code & 0x1F) | ((code & 0x40) ? 0x20 : 0));
            a.held = pattern;
            drawMosaic(pattern);
        } else {
            drawAlpha(code);
        }
    }
}

// Dibuja la linea de estado (fila rows+2) y deja el cursor para que update() lo recoloque.
void TeletextTerminal::statusRow(const System2Machine& m, const char* text, bool fromProgmem) {
    uint8_t rows = m.crtc().rowsDisplayed();
    if (rows == 0 || rows > kMaxRows) rows = kMaxRows;
    moveTo((uint8_t)(rows + 2), 1);
    putStr("\x1b[0m\x1b[2K");
    if (fromProgmem) puts_p(text); else putStr(text);
    cur_fg_ = cur_bg_ = 255;
    cursor_known_ = false;
}

void TeletextTerminal::setStatus(const System2Machine& m, const char* text) { statusRow(m, text, false); }
void TeletextTerminal::showHelp(const System2Machine& m) { statusRow(m, kStatusText, true); }

void TeletextTerminal::redraw(const System2Machine& m) {
    putStr("\x1b[0m\x1b[?7l\x1b[2J\x1b[H");
    cur_fg_ = cur_bg_ = 255;
    shadow_valid_ = false;
    cursor_known_ = false;
    showHelp(m);
    update(m);
}

void TeletextTerminal::update(const System2Machine& m) {
    uint8_t cols = m.crtc().charsPerRow();
    if (cols == 0 || cols > kMaxCols) cols = kMaxCols;
    uint8_t rows = m.crtc().rowsDisplayed();
    if (rows == 0 || rows > kMaxRows) rows = kMaxRows;

    const uint8_t* vram = m.videoRam();
    const uint16_t start = m.crtc().startAddress();
    uint8_t cells[kMaxCols];
    bool began = false;

    for (uint8_t r = 0; r < rows; r++) {
        for (uint8_t c = 0; c < cols; c++) {
            cells[c] = vram[(uint16_t)(start + (uint16_t)r * cols + c) & (System2Machine::kVideoRamSize - 1)];
        }
        if (shadow_valid_ && memcmp(shadow_[r], cells, cols) == 0) continue;
        if (!began) { putStr("\x1b[?25l"); began = true; cursor_known_ = false; }
        renderRow(r, cells, cols);
        memcpy(shadow_[r], cells, cols);
    }
    shadow_valid_ = true;

    // Cursor del 6845 (R14:R15); R10 bits 6-5 = 01 -> sin cursor.
    const uint16_t pos = (uint16_t)(m.crtc().cursorAddress() - start) & (System2Machine::kVideoRamSize - 1);
    const uint8_t row = (uint8_t)(pos / cols);
    const uint8_t col = (uint8_t)(pos % cols);
    const bool visible = row < rows && (((m.crtc().reg(10) >> 5) & 3) != 1);

    if (began || !cursor_known_ || row != cursor_row_ || col != cursor_col_ || visible != cursor_visible_) {
        if (began) { putStr("\x1b[0m"); cur_fg_ = cur_bg_ = 255; }
        if (visible) {
            moveTo((uint8_t)(row + 1), (uint8_t)(col + 1));
            putStr("\x1b[?25h");
        } else if (!began) {
            putStr("\x1b[?25l");
        }
        cursor_row_ = row; cursor_col_ = col; cursor_visible_ = visible; cursor_known_ = true;
    }
}
