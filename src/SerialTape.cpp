#include "SerialTape.h"

#include <string.h>

SerialTape::SerialTape(ByteSink& out)
    : out_(out), mode_(MODE_IDLE), awaiting_(REQ_NONE), ended_(false), head_(0), count_(0),
      lead_(false), lead_next_(false), eof_(false), eof_reported_(false), abort_(false),
      out_n_(0), leader_pending_(false), last_op_(0), line_n_(0), overflow_(false) {}

void SerialTape::str(const char* s) {
    while (*s) out_.put((uint8_t)*s++);
}

void SerialTape::hex2(uint8_t v) {
    static const char digits[] = "0123456789ABCDEF";
    out_.put((uint8_t)digits[v >> 4]);
    out_.put((uint8_t)digits[v & 15]);
}

void SerialTape::reset() {
    if (mode_ != MODE_IDLE) endSession();
    head_ = count_ = 0;
    lead_ = lead_next_ = eof_ = eof_reported_ = abort_ = false;
    awaiting_ = REQ_NONE;
    leader_pending_ = false;
    out_n_ = 0;
    line_n_ = 0;
    overflow_ = false;
}

void SerialTape::startSession(Mode m) {
    mode_ = m;
    awaiting_ = REQ_NONE;
    abort_ = false;
    eof_ = false;
    eof_reported_ = false;
    head_ = count_ = 0;
    lead_ = lead_next_ = false;
    out_n_ = 0;
    line_n_ = 0;
    overflow_ = false;
    // Debajo de la linea de estado, para no pisar la pantalla del System 2.
    str("\x1b[0m\x1b[?25h\x1b[29;1H\x1b[J");
    str(m == MODE_IN ? "#TAPE IN\r\n" : "#TAPE OUT\r\n");
}

void SerialTape::endSession() {
    if (mode_ == MODE_OUT) flushLine();
    str("#END\r\n");
    mode_ = MODE_IDLE;
    awaiting_ = REQ_NONE;
    head_ = count_ = 0;
    lead_ = lead_next_ = false;
    out_n_ = 0;
    leader_pending_ = false;
    ended_ = true;
}

void SerialTape::ensure(Mode m, uint32_t insns) {
    last_op_ = insns;
    if (mode_ == m) return;
    if (mode_ != MODE_IDLE) endSession();
    startSession(m);
}

void SerialTape::request(Request r) {
    if (awaiting_ == r) return;
    awaiting_ = r;
    str(r == REQ_SYNC ? "#SYNC\r" : "#NEED\r");
}

SerialTape::SyncResult SerialTape::sync(uint32_t insns) {
    ensure(MODE_IN, insns);
    if (abort_) {
        abort_ = false;
        return SYNC_ABORT;
    }
    if (count_ > 0 && lead_) {  // llego un bloque nuevo entero
        lead_ = false;
        return SYNC_READY;
    }
    head_ = count_ = 0;         // el COS se salta lo que quede del bloque actual
    if (eof_) return SYNC_ABORT;
    request(REQ_SYNC);
    return SYNC_WAIT;
}

bool SerialTape::getByte(uint8_t* b, uint32_t insns) {
    ensure(MODE_IN, insns);
    if (count_ == 0) {
        if (eof_) {
            if (!eof_reported_) {
                eof_reported_ = true;
                str("#EOF\r\n");
            }
            return false;
        }
        request(REQ_NEED);
        return false;
    }
    *b = fifo_[head_++];
    count_--;
    lead_ = false;
    return true;
}

void SerialTape::putByte(uint8_t b, uint32_t insns) {
    ensure(MODE_OUT, insns);
    if (leader_pending_) {
        flushLine();
        str("L\r\n");
        leader_pending_ = false;
    }
    out_buf_[out_n_++] = b;
    if (out_n_ == kChunk) flushLine();
}

void SerialTape::flushLine() {
    if (out_n_ == 0) return;
    uint8_t sum = 0;
    out_.put('D');
    for (uint8_t i = 0; i < out_n_; i++) {
        hex2(out_buf_[i]);
        sum = (uint8_t)(sum + out_buf_[i]);
    }
    hex2(sum);
    str("\r\n");
    out_n_ = 0;
}

void SerialTape::onPrompt() {
    if (mode_ != MODE_IDLE) endSession();
}

void SerialTape::service(uint32_t insns) {
    if (mode_ != MODE_IDLE && (uint32_t)(insns - last_op_) > kIdleInsns) endSession();
}

// ---------------------------------------------------------------------------
// Lineas recibidas del PC
// ---------------------------------------------------------------------------

void SerialTape::feed(uint8_t c) {
    if (c == '\r' || c == '\n') {
        if (line_n_ > 0 || overflow_) processLine();
        line_n_ = 0;
        overflow_ = false;
        return;
    }
    if (line_n_ == 0 && !overflow_ && c == ' ') {  // ESPACIO al principio de linea: cancelar
        abort_ = true;
        return;
    }
    if (line_n_ < kLineMax) line_[line_n_++] = (char)c;
    else overflow_ = true;
}

void SerialTape::error() {
    str("#ERR\r\n");
    awaiting_ = REQ_NONE;  // la CPU volvera a pedir; el PC reenvia la ultima linea
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void SerialTape::parseData() {
    const uint8_t digits = (uint8_t)(line_n_ - 1);
    if (digits < 2 || (digits & 1)) { error(); return; }
    const uint8_t n = (uint8_t)(digits / 2 - 1);
    if (n > kChunk || count_ != 0) { error(); return; }

    uint8_t tmp[kChunk];
    uint8_t sum = 0;
    for (uint8_t i = 0; i < n; i++) {
        const int hi = hexVal(line_[1 + 2 * i]);
        const int lo = hexVal(line_[2 + 2 * i]);
        if (hi < 0 || lo < 0) { error(); return; }
        tmp[i] = (uint8_t)((hi << 4) | lo);
        sum = (uint8_t)(sum + tmp[i]);
    }
    const int ckHi = hexVal(line_[1 + 2 * n]);
    const int ckLo = hexVal(line_[2 + 2 * n]);
    if (ckHi < 0 || ckLo < 0 || (uint8_t)((ckHi << 4) | ckLo) != sum) { error(); return; }

    memcpy(fifo_, tmp, n);
    head_ = 0;
    count_ = n;
    lead_ = lead_next_;
    lead_next_ = false;
    awaiting_ = REQ_NONE;
}

void SerialTape::processLine() {
    if (overflow_) { error(); return; }
    switch (line_[0]) {
        case 'D': parseData(); break;
        case 'L': lead_next_ = true; break;
        case 'E':
            eof_ = true;
            awaiting_ = REQ_NONE;
            break;
        case 'R':
            head_ = count_ = 0;
            lead_ = lead_next_ = eof_ = eof_reported_ = false;
            awaiting_ = REQ_NONE;
            break;
        default: error(); break;
    }
}
