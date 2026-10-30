#pragma once

#include <stdint.h>

#include "config.h"

// Resultado de un "trap" (rutina de la ROM sustituida por codigo C++).
enum HookResult : uint8_t { HOOK_NONE, HOOK_DONE, HOOK_WAIT };

struct Cpu6502Defs {
    enum Flag : uint8_t {
        FLAG_C = 0x01,
        FLAG_Z = 0x02,
        FLAG_I = 0x04,
        FLAG_D = 0x08,
        FLAG_B = 0x10,
        FLAG_U = 0x20,
        FLAG_V = 0x40,
        FLAG_N = 0x80
    };

    // Aritmetica decimal NMOS fuera de linea (rara: el bucle principal no la inlinea).
    struct DecResult { uint8_t a, c, v, n, z; };
    static DecResult adcDecimal(uint8_t a, uint8_t m, uint8_t carry) __attribute__((noinline));
    static uint8_t sbcDecimal(uint8_t a, uint8_t m, uint8_t carry) __attribute__((noinline));
};

// 6502 NMOS (solo opcodes documentados; los no documentados se ejecutan como
// NOP de 1 byte y quedan anotados en illegalCount()/lastIllegalPc()). Incluye
// modo decimal (ADC/SBC) con el comportamiento de flags del NMOS.
// Sin STL y sin tablas en RAM: apto para AVR (Arduino Mega).
//
// Es una plantilla sobre la memoria M para que los accesos se compilen en
// linea (sin llamadas virtuales): en un AVR a 16 MHz cada llamada virtual cuesta
// mas que la propia instruccion 6502. M debe ofrecer:
//   uint8_t rd(uint16_t), void wr(uint16_t, uint8_t)   acceso a datos
//   uint8_t fetchOp(uint16_t pc)   byte de codigo de operacion (puede estar "parcheado" con 0x02)
//   uint8_t fetchByte(uint16_t pc) byte de operando
//   uint8_t* lowRam()              RAM lineal desde $0000 (pagina cero y pila viven ahi siempre)
//   static const bool kTraps; si es true: origOp(pc), HookResult trap(pc)
//
// El bucle run() mantiene registros y flags en variables locales (registros del
// AVR) y cuenta los ciclos con una constante por opcode.
// Flags: C, V, D e I se guardan como 0/1; N y Z de forma perezosa: N = bit 7
// de fn, Z = (fz == 0).
template <class M>
class Cpu6502 : public Cpu6502Defs {
public:
    explicit Cpu6502(M& mem)
        : m_(mem), pc_(0), a_(0), x_(0), y_(0), sp_(0xFD), fc_(0), fv_(0), fd_(0), fi_(1), fn_(0), fz_(1),
          nmi_pending_(false), irq_line_(false), ext_(0), insn_count_(0), cycles_(0), illegal_count_(0),
          last_illegal_pc_(0) {}

    void reset() {
        a_ = x_ = y_ = 0;
        sp_ = 0xFD;
        setStatus(FLAG_I | FLAG_U);
        nmi_pending_ = false;
        ext_ = irq_line_ ? 1 : 0;
        pc_ = read16(0xFFFC);
    }
    // Ejecuta hasta n instrucciones (o hasta que un trap pida esperar). Devuelve
    // las que ejecuto de verdad.
    uint16_t run(uint16_t n) {
        uint16_t done = 0;
        while (n) {
            const uint8_t chunk = n > 255 ? 255 : (uint8_t)n;   // contador de 8 bits (un registro) y acumulador de ciclos de 16
            const uint8_t r = runChunk(chunk);
            done = (uint16_t)(done + r);
            if (r < chunk) break;
            n = (uint16_t)(n - chunk);
        }
        return done;
    }
    void step() { runChunk(1); }

    void nmi() { nmi_pending_ = true; ext_ = 1; }               // flanco NMI (se atiende en la proxima instruccion)
    void setIrq(bool level) { irq_line_ = level; ext_ = (nmi_pending_ || irq_line_) ? 1 : 0; }

    void setPc(uint16_t pc) { pc_ = pc; }

    // --- API para sustituir rutinas de la ROM por codigo C++ (emulacion de
    // alto nivel, ver System2Machine::trap). Hacen lo mismo que la instruccion
    // real que se salta, sin consumir ciclos ni instrucciones.
    void setA(uint8_t v) { a_ = v; }
    void setX(uint8_t v) { x_ = v; }
    bool carry() const { return fc_ != 0; }
    void setStatus(uint8_t p) {
        fc_ = p & 1;
        fz_ = (p & 2) ? 0 : 1;
        fi_ = (p >> 2) & 1;
        fd_ = (p >> 3) & 1;
        fv_ = (p >> 6) & 1;
        fn_ = p;
    }
    uint8_t status() const {
        return (uint8_t)(FLAG_U | fc_ | (fz_ ? 0 : FLAG_Z) | (fi_ << 2) | (fd_ << 3) | (fv_ << 6) | (fn_ & FLAG_N));
    }
    uint8_t statusForPush() const { return (uint8_t)(status() | FLAG_B); }   // lo que apila PHP
    void pushByte(uint8_t v) { push(v); }
    void pushWord(uint16_t v) { push16(v); }
    void returnFromSubroutine() { pc_ = (uint16_t)(pull16() + 1); }            // como un RTS
    // Estado completo; lo usan los tests paso a paso.
    void setState(uint16_t pc, uint8_t sp, uint8_t a, uint8_t x, uint8_t y, uint8_t p) {
        pc_ = pc; sp_ = sp; a_ = a; x_ = x; y_ = y; setStatus(p);
    }

    uint16_t pc() const { return pc_; }
    uint8_t a() const { return a_; }
    uint8_t x() const { return x_; }
    uint8_t y() const { return y_; }
    uint8_t sp() const { return sp_; }
    uint32_t instructionCount() const { return insn_count_; }
    // Ciclos de reloj de un 6502 NMOS real consumidos hasta ahora (tabla de
    // tiempos base + 1 por cruce de pagina en lecturas indexadas + 1/2 por salto
    // tomado/con cruce de pagina; 7 por interrupcion). Sin reiniciar con reset().
    uint32_t cycles() const { return cycles_; }
    uint16_t illegalCount() const { return illegal_count_; }
    uint16_t lastIllegalPc() const { return last_illegal_pc_; }

private:
    M& m_;
    uint16_t pc_;
    uint8_t a_, x_, y_, sp_;
    uint8_t fc_, fv_, fd_, fi_, fn_, fz_;
    bool nmi_pending_;
    bool irq_line_;
    uint8_t ext_;                 // nmi_pending_ || irq_line_ (una sola comprobacion por instruccion)
    uint32_t insn_count_;
    uint32_t cycles_;
    uint16_t illegal_count_;
    uint16_t last_illegal_pc_;

    // Rutas lentas (interrupciones, utilidades de los hooks): sobre los miembros.
    void push(uint8_t v) { m_.wr((uint16_t)(0x0100 | sp_), v); sp_--; }
    uint8_t pull() { sp_++; return m_.rd((uint16_t)(0x0100 | sp_)); }
    void push16(uint16_t v) { push((uint8_t)(v >> 8)); push((uint8_t)(v & 0xFF)); }
    uint16_t pull16() {
        const uint8_t lo = pull();
        const uint8_t hi = pull();
        return (uint16_t)(lo | (hi << 8));
    }
    uint16_t read16(uint16_t addr) {
        const uint8_t lo = m_.rd(addr);
        const uint8_t hi = m_.rd((uint16_t)(addr + 1));
        return (uint16_t)(lo | (hi << 8));
    }
    void interrupt(uint16_t vector, bool brk) {
        push16(pc_);
        push((uint8_t)((status() & ~FLAG_B) | FLAG_U | (brk ? FLAG_B : 0)));
        fi_ = 1;
        pc_ = read16(vector);
    }

    // Fuera de linea a proposito: con marco de pila propio (< 64 bytes) el AVR accede a las
    // variables locales desbordadas con LDD/STD directos; dentro de Console::poll cada acceso
    // costaba dos instrucciones mas.
    uint8_t runChunk(uint8_t n) __attribute__((noinline));
};

// ---------------------------------------------------------------------------
// Bucle principal.
// ---------------------------------------------------------------------------

#define SAVE_STATE()                                                           \
    do {                                                                       \
        pc_ = pc; a_ = a; x_ = x; y_ = y; sp_ = sp;                            \
        fc_ = fc; fv_ = fv; fd_ = fd; fi_ = fi; fn_ = fn; fz_ = fz;            \
    } while (0)
#define LOAD_STATE()                                                           \
    do {                                                                       \
        pc = pc_; a = a_; x = x_; y = y_; sp = sp_;                            \
        fc = fc_; fv = fv_; fd = fd_; fi = fi_; fn = fn_; fz = fz_;            \
    } while (0)

// Despacho por "computed goto": una tabla de 256 direcciones de etiqueta en flash
// (Cpu6502Dispatch.inc, generado por tools/gen_dispatch.py) en vez del switch, que
// en el AVR cuesta ~30 ciclos entre comprobacion de rango y __tablejump2__.
#define OPC(n) L_##n:
#ifdef __AVR__
#define JUMP_OP(op) goto *(void*)pgm_read_word(&kDispatch[(op)])
#else
#define JUMP_OP(op) goto *kDispatch[(op)]
#endif

#define OPB() (m_.fetchByte(pc++))
#define RD(ea) (m_.rd(ea))
#define WR(ea, v) m_.wr((ea), (v))
#define NZ(v) do { const uint8_t nz_ = (v); fn = nz_; fz = nz_; } while (0)
#define PUSH(v) do { ram[0x100 | sp] = (v); sp--; } while (0)
#define PULL(dst) do { sp++; (dst) = ram[0x100 | sp]; } while (0)
#define SET_P(p) do { fc = (p) & 1; fz = ((p) & 2) ? 0 : 1; fi = ((p) >> 2) & 1; fd = ((p) >> 3) & 1; fv = ((p) >> 6) & 1; fn = (p); } while (0)

// Direcciones efectivas (declaran ea).
#define EA_ABS(ea)                                                             \
    const uint8_t lo_ = OPB();                                                 \
    const uint8_t hi_ = OPB();                                                 \
    const uint16_t ea = (uint16_t)(lo_ | ((uint16_t)hi_ << 8))
#define EA_ABS_IDX(ea, r)                                                      \
    const uint8_t lo_ = OPB();                                                 \
    const uint8_t hi_ = OPB();                                                 \
    const uint16_t base_ = (uint16_t)(lo_ | ((uint16_t)hi_ << 8));             \
    const uint16_t ea = (uint16_t)(base_ + (r))
#define EA_IND_X(ea)                                                           \
    const uint8_t zp_ = (uint8_t)(OPB() + x);                                  \
    const uint8_t lo_ = ram[zp_];                                              \
    const uint8_t hi_ = ram[(uint8_t)(zp_ + 1)];                               \
    const uint16_t ea = (uint16_t)(lo_ | ((uint16_t)hi_ << 8))
#define EA_IND_Y(ea)                                                           \
    const uint8_t zp_ = OPB();                                                 \
    const uint8_t lo_ = ram[zp_];                                              \
    const uint8_t hi_ = ram[(uint8_t)(zp_ + 1)];                               \
    const uint16_t base_ = (uint16_t)(lo_ | ((uint16_t)hi_ << 8));             \
    const uint16_t ea = (uint16_t)(base_ + y)
#define CROSS() cyc = (uint16_t)(cyc + (((ea ^ base_) & 0xFF00) != 0))

// Familia "lee un valor": ORA/AND/EOR/ADC/SBC/CMP/LDA (imm, zp, zpx, abs, absx, absy, indx, indy).
#define ALU_GROUP(IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY, STMT)                                  \
    OPC(IMM) { cyc += 2; const uint8_t m = OPB(); STMT; continue; }                                   \
    OPC(ZP)  { cyc += 3; const uint8_t m = ram[OPB()]; STMT; continue; }                              \
    OPC(ZPX) { cyc += 4; const uint8_t m = ram[(uint8_t)(OPB() + x)]; STMT; continue; }               \
    OPC(ABS) { cyc += 4; EA_ABS(ea); const uint8_t m = RD(ea); STMT; continue; }                      \
    OPC(ABSX) { cyc += 4; EA_ABS_IDX(ea, x); CROSS(); const uint8_t m = RD(ea); STMT; continue; }     \
    OPC(ABSY) { cyc += 4; EA_ABS_IDX(ea, y); CROSS(); const uint8_t m = RD(ea); STMT; continue; }     \
    OPC(INDX) { cyc += 6; EA_IND_X(ea); const uint8_t m = RD(ea); STMT; continue; }                   \
    OPC(INDY) { cyc += 5; EA_IND_Y(ea); CROSS(); const uint8_t m = RD(ea); STMT; continue; }

#define LD_ZP(OP, STMT)   OPC(OP) { cyc += 3; const uint8_t m = ram[OPB()]; STMT; continue; }
#define LD_ZPI(OP, R, STMT) OPC(OP) { cyc += 4; const uint8_t m = ram[(uint8_t)(OPB() + (R))]; STMT; continue; }
#define LD_ABS(OP, STMT)  OPC(OP) { cyc += 4; EA_ABS(ea); const uint8_t m = RD(ea); STMT; continue; }
#define LD_ABSI(OP, R, STMT) OPC(OP) { cyc += 4; EA_ABS_IDX(ea, R); CROSS(); const uint8_t m = RD(ea); STMT; continue; }
#define LD_IMM(OP, STMT)  OPC(OP) { cyc += 2; const uint8_t m = OPB(); STMT; continue; }

// Almacenamientos.
#define ST_ZP(OP, V)      OPC(OP) { cyc += 3; ram[OPB()] = (V); continue; }
#define ST_ZPI(OP, R, V)  OPC(OP) { cyc += 4; ram[(uint8_t)(OPB() + (R))] = (V); continue; }
#define ST_ABS(OP, V)     OPC(OP) { cyc += 4; EA_ABS(ea); WR(ea, (V)); continue; }
#define ST_ABSI(OP, R, V) OPC(OP) { cyc += 5; EA_ABS_IDX(ea, R); WR(ea, (V)); continue; }
#define ST_INDX(OP, V)    OPC(OP) { cyc += 6; EA_IND_X(ea); WR(ea, (V)); continue; }
#define ST_INDY(OP, V)    OPC(OP) { cyc += 6; EA_IND_Y(ea); WR(ea, (V)); continue; }

// Lectura-modificacion-escritura: FN(v) modifica v.
#define RMW_GROUP(ZP, ZPX, ABS, ABSX, FN)                                                           \
    OPC(ZP)  { cyc += 5; const uint8_t zp_ = OPB(); uint8_t v = ram[zp_]; FN(v); ram[zp_] = v; continue; } \
    OPC(ZPX) { cyc += 6; const uint8_t zp_ = (uint8_t)(OPB() + x); uint8_t v = ram[zp_]; FN(v); ram[zp_] = v; continue; } \
    OPC(ABS) { cyc += 6; EA_ABS(ea); uint8_t v = RD(ea); FN(v); WR(ea, v); continue; }                \
    OPC(ABSX) { cyc += 7; EA_ABS_IDX(ea, x); uint8_t v = RD(ea); FN(v); WR(ea, v); continue; }

#define F_ASL(v) do { fc = (uint8_t)((v) >> 7); (v) = (uint8_t)((v) << 1); NZ(v); } while (0)
#define F_LSR(v) do { fc = (uint8_t)((v) & 1); (v) = (uint8_t)((v) >> 1); NZ(v); } while (0)
#define F_ROL(v) do { const uint8_t c_ = fc; fc = (uint8_t)((v) >> 7); (v) = (uint8_t)(((v) << 1) | c_); NZ(v); } while (0)
#define F_ROR(v) do { const uint8_t c_ = fc; fc = (uint8_t)((v) & 1); (v) = (uint8_t)(((v) >> 1) | (c_ << 7)); NZ(v); } while (0)
#define F_INC(v) do { (v) = (uint8_t)((v) + 1); NZ(v); } while (0)
#define F_DEC(v) do { (v) = (uint8_t)((v) - 1); NZ(v); } while (0)

#define COMPARE(r, m) do { const uint8_t d_ = (uint8_t)((r) - (m)); fc = ((r) >= (m)) ? 1 : 0; NZ(d_); } while (0)

#define DO_ADC(m)                                                                                   \
    do {                                                                                            \
        if (!fd) {                                                                                  \
            const uint16_t bin_ = (uint16_t)a + (m) + fc;                                           \
            fv = (uint8_t)(((~(a ^ (m)) & (a ^ bin_)) >> 7) & 1);                                   \
            fc = (uint8_t)(bin_ >> 8);                                                              \
            a = (uint8_t)bin_;                                                                      \
            NZ(a);                                                                                  \
        } else {                                                                                    \
            const DecResult r_ = adcDecimal(a, (m), fc);                                            \
            a = r_.a; fc = r_.c; fv = r_.v; fn = r_.n; fz = r_.z;                                   \
        }                                                                                           \
    } while (0)

// En NMOS todos los flags del SBC salen del resultado binario.
#define DO_SBC(m)                                                                                   \
    do {                                                                                            \
        const uint8_t cin_ = fc;                                                                    \
        const uint16_t bin_ = (uint16_t)a + (uint8_t)~(m) + cin_;                                   \
        const uint8_t res_ = (uint8_t)bin_;                                                         \
        fv = (uint8_t)((((a ^ (m)) & (a ^ bin_)) >> 7) & 1);                                        \
        fc = (uint8_t)(bin_ >> 8);                                                                  \
        if (fd) a = sbcDecimal(a, (m), cin_); else a = res_;                                        \
        NZ(res_);                                                                                   \
    } while (0)

#define BRANCH(cond)                                                                                \
    {                                                                                               \
        cyc += 2;                                                                                   \
        const int8_t off_ = (int8_t)OPB();                                                          \
        if (cond) {                                                                                 \
            const uint16_t t_ = (uint16_t)(pc + off_);                                              \
            cyc += (((pc ^ t_) & 0xFF00) ? 2 : 1);                                                  \
            pc = t_;                                                                                \
        }                                                                                           \
        continue;                                                                                      \
    }

#define PACK_P() ((uint8_t)(FLAG_U | fc | (fz ? 0 : FLAG_Z) | (fi << 2) | (fd << 3) | (fv << 6) | (fn & FLAG_N)))

template <class M>
uint8_t Cpu6502<M>::runChunk(uint8_t n) {
#ifndef CPU6502_GEN
    static const void* const kDispatch[256] PROGMEM = {
#include "Cpu6502Dispatch.inc"
    };
#endif
    uint16_t pc;
    uint8_t a, x, y, sp, fc, fv, fd, fi, fn, fz;
    LOAD_STATE();
    uint8_t* const ram = m_.lowRam();
    uint16_t cyc = 0;
    uint8_t left = n;
    uint8_t base = n;     // instrucciones de este bloque aun no sumadas a insn_count_
    uint8_t ext = ext_;   // copia local de ext_ (se refresca tras interrupciones y traps)

    while (left) {
        if (ext) {
            if (nmi_pending_) {
                nmi_pending_ = false;
                ext = ext_ = irq_line_ ? 1 : 0;
                SAVE_STATE();
                interrupt(0xFFFA, false);
                LOAD_STATE();
                cyc += 7;
                left--;
                continue;
            }
            if (irq_line_ && !fi) {
                SAVE_STATE();
                interrupt(0xFFFE, false);
                LOAD_STATE();
                cyc += 7;
                left--;
                continue;
            }
        }

        left--;
        uint8_t op = m_.fetchOp(pc);
        pc++;
        JUMP_OP(op);
            ALU_GROUP(0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11, a |= m; NZ(a))
            ALU_GROUP(0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31, a &= m; NZ(a))
            ALU_GROUP(0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51, a ^= m; NZ(a))
            ALU_GROUP(0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71, DO_ADC(m))
            ALU_GROUP(0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1, DO_SBC(m))
            ALU_GROUP(0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1, COMPARE(a, m))
            ALU_GROUP(0xA9, 0xA5, 0xB5, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1, a = m; NZ(a))

            ST_ZP(0x85, a)
            ST_ZPI(0x95, x, a)
            ST_ABS(0x8D, a)
            ST_ABSI(0x9D, x, a)
            ST_ABSI(0x99, y, a)
            ST_INDX(0x81, a)
            ST_INDY(0x91, a)

            LD_IMM(0xA2, x = m; NZ(x))
            LD_ZP(0xA6, x = m; NZ(x))
            LD_ZPI(0xB6, y, x = m; NZ(x))
            LD_ABS(0xAE, x = m; NZ(x))
            LD_ABSI(0xBE, y, x = m; NZ(x))

            LD_IMM(0xA0, y = m; NZ(y))
            LD_ZP(0xA4, y = m; NZ(y))
            LD_ZPI(0xB4, x, y = m; NZ(y))
            LD_ABS(0xAC, y = m; NZ(y))
            LD_ABSI(0xBC, x, y = m; NZ(y))

            ST_ZP(0x86, x)
            ST_ZPI(0x96, y, x)
            ST_ABS(0x8E, x)
            ST_ZP(0x84, y)
            ST_ZPI(0x94, x, y)
            ST_ABS(0x8C, y)

            LD_IMM(0xE0, COMPARE(x, m))
            LD_ZP(0xE4, COMPARE(x, m))
            LD_ABS(0xEC, COMPARE(x, m))
            LD_IMM(0xC0, COMPARE(y, m))
            LD_ZP(0xC4, COMPARE(y, m))
            LD_ABS(0xCC, COMPARE(y, m))

            LD_ZP(0x24, fz = a & m; fn = m; fv = (uint8_t)((m >> 6) & 1))
            LD_ABS(0x2C, fz = a & m; fn = m; fv = (uint8_t)((m >> 6) & 1))

            OPC(0x0A) cyc += 2; F_ASL(a); continue;
            OPC(0x4A) cyc += 2; F_LSR(a); continue;
            OPC(0x2A) cyc += 2; F_ROL(a); continue;
            OPC(0x6A) cyc += 2; F_ROR(a); continue;

            RMW_GROUP(0x06, 0x16, 0x0E, 0x1E, F_ASL)
            RMW_GROUP(0x46, 0x56, 0x4E, 0x5E, F_LSR)
            RMW_GROUP(0x26, 0x36, 0x2E, 0x3E, F_ROL)
            RMW_GROUP(0x66, 0x76, 0x6E, 0x7E, F_ROR)
            RMW_GROUP(0xE6, 0xF6, 0xEE, 0xFE, F_INC)
            RMW_GROUP(0xC6, 0xD6, 0xCE, 0xDE, F_DEC)

            OPC(0xE8) cyc += 2; x++; NZ(x); continue;
            OPC(0xC8) cyc += 2; y++; NZ(y); continue;
            OPC(0xCA) cyc += 2; x--; NZ(x); continue;
            OPC(0x88) cyc += 2; y--; NZ(y); continue;

            OPC(0xAA) cyc += 2; x = a; NZ(x); continue;
            OPC(0x8A) cyc += 2; a = x; NZ(a); continue;
            OPC(0xA8) cyc += 2; y = a; NZ(y); continue;
            OPC(0x98) cyc += 2; a = y; NZ(a); continue;
            OPC(0xBA) cyc += 2; x = sp; NZ(x); continue;
            OPC(0x9A) cyc += 2; sp = x; continue;

            OPC(0x48) cyc += 3; PUSH(a); continue;
            OPC(0x68) cyc += 4; PULL(a); NZ(a); continue;
            OPC(0x08) cyc += 3; PUSH((uint8_t)(PACK_P() | FLAG_B)); continue;
            OPC(0x28) {
                cyc += 4;
                uint8_t p;
                PULL(p);
                SET_P(p);
                continue;
            }

            OPC(0x4C) {
                cyc += 3;
                EA_ABS(ea);
                pc = ea;
                continue;
            }
            OPC(0x6C) {
                cyc += 5;
                EA_ABS(ptr);
                const uint8_t lo = RD(ptr);
                const uint8_t hi = RD((uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));  // bug de cruce de pagina del NMOS
                pc = (uint16_t)(lo | (hi << 8));
                continue;
            }
            OPC(0x20) {
                // Como el chip: lee el byte bajo, apila (la direccion del byte alto), y
                // solo entonces lee el byte alto; si la pila pisa el operando se ve el cambio.
                cyc += 6;
                const uint8_t lo = OPB();
                PUSH((uint8_t)(pc >> 8));
                PUSH((uint8_t)pc);
                const uint8_t hi = OPB();
                pc = (uint16_t)(lo | (hi << 8));
                continue;
            }
            OPC(0x60) {
                cyc += 6;
                uint8_t lo, hi;
                PULL(lo);
                PULL(hi);
                pc = (uint16_t)((lo | (hi << 8)) + 1);
                continue;
            }
            OPC(0x40) {
                cyc += 6;
                uint8_t p, lo, hi;
                PULL(p);
                SET_P(p);
                PULL(lo);
                PULL(hi);
                pc = (uint16_t)(lo | (hi << 8));
                continue;
            }
            OPC(0x00)
                cyc += 7;
                pc++;  // BRK deja un byte de relleno
                SAVE_STATE();
                interrupt(0xFFFE, true);
                LOAD_STATE();
                continue;

            OPC(0x10) BRANCH(!(fn & 0x80))
            OPC(0x30) BRANCH((fn & 0x80) != 0)
            OPC(0x50) BRANCH(!fv)
            OPC(0x70) BRANCH(fv)
            OPC(0x90) BRANCH(!fc)
            OPC(0xB0) BRANCH(fc)
            OPC(0xD0) BRANCH(fz != 0)
            OPC(0xF0) BRANCH(fz == 0)

            OPC(0x18) cyc += 2; fc = 0; continue;
            OPC(0x38) cyc += 2; fc = 1; continue;
            OPC(0x58) cyc += 2; fi = 0; continue;
            OPC(0x78) cyc += 2; fi = 1; continue;
            OPC(0xB8) cyc += 2; fv = 0; continue;
            OPC(0xD8) cyc += 2; fd = 0; continue;
            OPC(0xF8) cyc += 2; fd = 1; continue;

            OPC(0xEA) cyc += 2; continue;

            // Opcode de captura: la ROM esta "parcheada" en las rutinas que el emulador sustituye.
            OPC(0x02)
                if (M::kTraps) {
                    const uint16_t opc = (uint16_t)(pc - 1);
                    left++;                                  // el trap no es una instruccion
                    pc = opc;
                    SAVE_STATE();
                    insn_count_ += (uint8_t)(base - left);
                    base = left;
                    const HookResult r = m_.trap(opc);
                    LOAD_STATE();
                    ext = ext_;
                    if (r == HOOK_DONE) continue;
                    if (r == HOOK_WAIT) goto finish;
                    // HOOK_NONE: se ejecuta la instruccion original.
                    op = m_.origOp(opc);
                    pc = (uint16_t)(opc + 1);
                    left--;
                    if (op != 0x02) JUMP_OP(op);
                }
                /* fallthrough */
            L_illegal:
                cyc += 2;
                illegal_count_++;
                last_illegal_pc_ = (uint16_t)(pc - 1);
                continue;
    }

finish:
    SAVE_STATE();
    cycles_ += cyc;
    insn_count_ += (uint8_t)(base - left);
    return (uint8_t)(n - left);
}
