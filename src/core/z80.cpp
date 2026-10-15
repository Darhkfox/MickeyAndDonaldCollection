// Zilog Z80 interpreter. Flag behaviour, including the undocumented X/Y bits
// (3 and 5), follows Sean Young's "The Undocumented Z80 Documented". MEMPTR
// (wz) is tracked because it leaks into the X/Y flags of BIT n,(HL).
#include "z80.h"
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

enum : uint8_t { CF = 0x01, NF = 0x02, PF = 0x04, XF = 0x08, HF = 0x10, YF = 0x20, ZF = 0x40, SF = 0x80 };

struct FlagTables {
    uint8_t sz[256];    // S, Z, Y, X of a result byte
    uint8_t szp[256];   // ... plus P/V = even parity
    constexpr FlagTables() : sz(), szp() {
        for (int v = 0; v < 256; v++) {
            int p = v ^ (v >> 4);
            p ^= p >> 2;
            p ^= p >> 1;
            sz[v] = uint8_t((v & (SF | YF | XF)) | (v ? 0 : ZF));
            szp[v] = uint8_t(sz[v] | ((p & 1) ? 0 : PF));
        }
    }
};
constexpr FlagTables kFlags;

inline uint8_t SZ(unsigned v) { return kFlags.sz[v & 0xFF]; }
inline uint8_t SZP(unsigned v) { return kFlags.szp[v & 0xFF]; }

// Register pair halves (all supported hosts are little-endian).
inline uint8_t& hi(uint16_t& r) { return reinterpret_cast<uint8_t*>(&r)[1]; }
inline uint8_t& lo(uint16_t& r) { return reinterpret_cast<uint8_t*>(&r)[0]; }

struct State {
    uint16_t af, bc, de, hl, af2, bc2, de2, hl2, ix, iy, sp, pc, wz;
    uint8_t  i, r, im, iff1, iff2, halted, irq_line, nmi_pending;
    int32_t  ei_delay, wait_cycles;
};
static_assert(std::is_trivially_copyable<State>::value, "state block must be POD");

} // namespace

// ---------------------------------------------------------------------------
// Bus helpers

uint8_t Z80::fetch_op() {
    r = uint8_t((r & 0x80) | ((r + 1) & 0x7F));
    return read(ctx, pc++);
}

uint8_t Z80::fetch8() { return read(ctx, pc++); }

uint16_t Z80::fetch16() {
    const uint8_t l = read(ctx, pc++);
    const uint8_t h = read(ctx, pc++);
    return uint16_t(l | (h << 8));
}

uint16_t Z80::rd16(uint16_t a) {
    const uint8_t l = read(ctx, a);
    const uint8_t h = read(ctx, uint16_t(a + 1));
    return uint16_t(l | (h << 8));
}

void Z80::wr16(uint16_t a, uint16_t v) {
    write(ctx, a, uint8_t(v));
    write(ctx, uint16_t(a + 1), uint8_t(v >> 8));
}

void Z80::push(uint16_t v) {
    write(ctx, --sp, uint8_t(v >> 8));
    write(ctx, --sp, uint8_t(v));
}

uint16_t Z80::pop() {
    const uint8_t l = read(ctx, sp++);
    const uint8_t h = read(ctx, sp++);
    return uint16_t(l | (h << 8));
}

uint8_t Z80::port_in(uint16_t port) { return in ? in(ctx, port) : 0xFF; }

void Z80::port_out(uint16_t port, uint8_t v) {
    if (out) out(ctx, port, v);
}

// Operand encodings: r = B,C,D,E,H,L,-,A (6 is the memory operand, handled by
// callers); rp = BC,DE,HL,SP. Under a DD/FD prefix H/L/HL mean IXH/IXL/IX.
uint8_t& Z80::r8(int n, uint16_t& xy) {
    switch (n) {
    case 0:  return hi(bc);
    case 1:  return lo(bc);
    case 2:  return hi(de);
    case 3:  return lo(de);
    case 4:  return hi(xy);
    case 5:  return lo(xy);
    default: return hi(af);
    }
}

uint16_t& Z80::rp(int n, uint16_t& xy) {
    switch (n) {
    case 0:  return bc;
    case 1:  return de;
    case 2:  return xy;
    default: return sp;
    }
}

// cc = NZ,Z,NC,C,PO,PE,P,M
bool Z80::cond(int cc) const {
    static const uint8_t mask[4] = {ZF, CF, PF, SF};
    return ((af & mask[cc >> 1]) != 0) == ((cc & 1) != 0);
}

// ---------------------------------------------------------------------------
// ALU

// op = ADD,ADC,SUB,SBC,AND,XOR,OR,CP
void Z80::alu(int op, uint8_t v) {
    uint8_t& A = hi(af);
    uint8_t& F = lo(af);
    const unsigned a = A;
    unsigned res;
    switch (op) {
    case 0: case 1:
        res = a + v + (op == 1 ? (F & CF) : 0u);
        F = uint8_t(SZ(res) | ((a ^ v ^ res) & HF) | ((~(a ^ v) & (a ^ res) & 0x80) >> 5) | (res >> 8));
        A = uint8_t(res);
        return;
    case 2: case 3: case 7:
        res = a - v - (op == 3 ? (F & CF) : 0u);
        F = uint8_t(SZ(res) | NF | ((a ^ v ^ res) & HF) | (((a ^ v) & (a ^ res) & 0x80) >> 5) | ((res >> 8) & CF));
        if (op == 7)
            F = uint8_t((F & ~(YF | XF)) | (v & (YF | XF)));   // CP: X/Y come from the operand
        else
            A = uint8_t(res);
        return;
    case 4:  A &= v; F = uint8_t(SZP(A) | HF); return;
    case 5:  A ^= v; F = SZP(A); return;
    default: A |= v; F = SZP(A); return;
    }
}

uint8_t Z80::inc8(uint8_t v) {
    const uint8_t res = uint8_t(v + 1);
    lo(af) = uint8_t((lo(af) & CF) | SZ(res) | ((res & 0x0F) ? 0 : HF) | (v == 0x7F ? PF : 0));
    return res;
}

uint8_t Z80::dec8(uint8_t v) {
    const uint8_t res = uint8_t(v - 1);
    lo(af) = uint8_t((lo(af) & CF) | NF | SZ(res) | ((v & 0x0F) ? 0 : HF) | (v == 0x80 ? PF : 0));
    return res;
}

// op = RLC,RRC,RL,RR,SLA,SRA,SLL,SRL
uint8_t Z80::rot(int op, uint8_t v) {
    const unsigned cin = lo(af) & CF;
    unsigned res, c;
    switch (op) {
    case 0:  c = v >> 7; res = (v << 1) | c; break;
    case 1:  c = v & 1;  res = (v >> 1) | (c << 7); break;
    case 2:  c = v >> 7; res = (v << 1) | cin; break;
    case 3:  c = v & 1;  res = (v >> 1) | (cin << 7); break;
    case 4:  c = v >> 7; res = v << 1; break;
    case 5:  c = v & 1;  res = (v >> 1) | (v & 0x80); break;
    case 6:  c = v >> 7; res = (v << 1) | 1; break;   // SLL: undocumented, shifts in a 1
    default: c = v & 1;  res = v >> 1; break;
    }
    lo(af) = uint8_t(SZP(res) | c);
    return uint8_t(res);
}

// X/Y come from `xy_src`: the operand for registers, otherwise the high byte
// of MEMPTR / the (IX+d) address.
void Z80::bit(int n, uint8_t v, uint8_t xy_src) {
    const unsigned m = v & (1u << n);
    lo(af) = uint8_t((lo(af) & CF) | HF | (xy_src & (YF | XF)) | (m & SF) | (m ? 0 : ZF | PF));
}

uint16_t Z80::add16(uint16_t a, uint16_t b) {
    const unsigned res = unsigned(a) + b;
    lo(af) = uint8_t((lo(af) & (SF | ZF | PF)) | (((a ^ b ^ res) >> 8) & HF) | ((res >> 8) & (YF | XF)) | (res >> 16));
    wz = uint16_t(a + 1);
    return uint16_t(res);
}

void Z80::adc16(uint16_t v, bool sub) {
    const unsigned a = hl, c = af & CF;
    const unsigned res = sub ? a - v - c : a + v + c;
    const unsigned ov = sub ? (a ^ v) & (a ^ res) : ~(a ^ v) & (a ^ res);
    lo(af) = uint8_t(((res >> 8) & (SF | YF | XF)) | ((res & 0xFFFF) ? 0 : ZF) | (((a ^ v ^ res) >> 8) & HF) |
                     ((ov >> 13) & PF) | ((res >> 16) & CF) | (sub ? NF : 0));
    wz = uint16_t(a + 1);
    hl = uint16_t(res);
}

void Z80::daa() {
    uint8_t& A = hi(af);
    uint8_t& F = lo(af);
    const uint8_t a = A;
    uint8_t diff = 0, c = F & CF, h;
    if ((F & HF) || (a & 0x0F) > 9) diff = 0x06;
    if (c || a > 0x99) { diff |= 0x60; c = CF; }
    if (F & NF) {
        h = ((F & HF) && (a & 0x0F) < 6) ? HF : 0;
        A = uint8_t(a - diff);
    } else {
        h = ((a & 0x0F) > 9) ? HF : 0;
        A = uint8_t(a + diff);
    }
    F = uint8_t(SZP(A) | (F & NF) | h | c);
}

// ---------------------------------------------------------------------------
// Decoder

int Z80::step() {
    uint16_t* xy = &hl;
    int t = 0;
    uint8_t op = fetch_op();
    while (op == 0xDD || op == 0xFD) {
        // In a chain of index prefixes only the last one counts; each costs 4.
        xy = op == 0xDD ? &ix : &iy;
        t += 4;
        op = fetch_op();
        if (t >= 256 && (op == 0xDD || op == 0xFD)) {
            // Endless prefix run: un-fetch this prefix and yield so run() can return.
            pc--;
            r = uint8_t((r & 0x80) | ((r - 1) & 0x7F));
            return t;
        }
    }
    if (op == 0xCB) return t + (xy == &hl ? exec_cb() : exec_xycb(*xy));
    if (op == 0xED) return t + exec_ed();   // a DD/FD before ED is ignored
    return t + exec_main(op, *xy);
}

int Z80::exec_main(uint8_t op, uint16_t& xy) {
    uint8_t& A = hi(af);
    uint8_t& F = lo(af);
    const bool idx = &xy != &hl;
    // Under a prefix, (HL) becomes (IX+d): d is fetched here and the address
    // calculation costs 8 extra T-states (5 for LD (IX+d),n).
    const int ext = idx ? 8 : 0;
    auto ea = [&]() -> uint16_t {
        if (!idx) return hl;
        wz = uint16_t(xy + int8_t(fetch8()));
        return wz;
    };

    const int y = (op >> 3) & 7, z = op & 7, p = y >> 1;

    if ((op & 0xC0) == 0x40) {                          // LD r,r' / HALT
        if (op == 0x76) { halted = true; return 4; }
        // With an (IX+d) operand the other register is the real H/L.
        if (z == 6) { const uint16_t a = ea(); r8(y, hl) = read(ctx, a); return 7 + ext; }
        if (y == 6) { const uint16_t a = ea(); write(ctx, a, r8(z, hl)); return 7 + ext; }
        r8(y, xy) = r8(z, xy);
        return 4;
    }
    if ((op & 0xC0) == 0x80) {                          // ALU A,r
        if (z == 6) { const uint16_t a = ea(); alu(y, read(ctx, a)); return 7 + ext; }
        alu(y, r8(z, xy));
        return 4;
    }

    switch (op) {
    case 0x00: return 4;                                                    // NOP
    case 0x01: case 0x11: case 0x21: case 0x31: rp(p, xy) = fetch16(); return 10;
    case 0x02: case 0x12: {                                                 // LD (BC/DE),A
        const uint16_t a = op == 0x02 ? bc : de;
        write(ctx, a, A);
        wz = uint16_t((A << 8) | ((a + 1) & 0xFF));
        return 7;
    }
    case 0x0A: case 0x1A: {                                                 // LD A,(BC/DE)
        const uint16_t a = op == 0x0A ? bc : de;
        A = read(ctx, a);
        wz = uint16_t(a + 1);
        return 7;
    }
    case 0x03: case 0x13: case 0x23: case 0x33: rp(p, xy)++; return 6;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: rp(p, xy)--; return 6;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
        uint8_t& reg = r8(y, xy);
        reg = inc8(reg);
        return 4;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
        uint8_t& reg = r8(y, xy);
        reg = dec8(reg);
        return 4;
    }
    case 0x34: { const uint16_t a = ea(); write(ctx, a, inc8(read(ctx, a))); return 11 + ext; }
    case 0x35: { const uint16_t a = ea(); write(ctx, a, dec8(read(ctx, a))); return 11 + ext; }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:
        r8(y, xy) = fetch8();
        return 7;
    case 0x36: { const uint16_t a = ea(); write(ctx, a, fetch8()); return idx ? 15 : 10; }
    case 0x07:                                                              // RLCA
        A = uint8_t((A << 1) | (A >> 7));
        F = uint8_t((F & (SF | ZF | PF)) | (A & (YF | XF | CF)));
        return 4;
    case 0x0F:                                                              // RRCA
        F = uint8_t((F & (SF | ZF | PF)) | (A & CF));
        A = uint8_t((A >> 1) | (A << 7));
        F |= A & (YF | XF);
        return 4;
    case 0x17: {                                                            // RLA
        const uint8_t c = A >> 7;
        A = uint8_t((A << 1) | (F & CF));
        F = uint8_t((F & (SF | ZF | PF)) | (A & (YF | XF)) | c);
        return 4;
    }
    case 0x1F: {                                                            // RRA
        const uint8_t c = A & 1;
        A = uint8_t((A >> 1) | (F << 7));
        F = uint8_t((F & (SF | ZF | PF)) | (A & (YF | XF)) | c);
        return 4;
    }
    case 0x08: std::swap(af, af2); return 4;
    case 0x09: case 0x19: case 0x29: case 0x39: xy = add16(xy, rp(p, xy)); return 11;
    case 0x10: {                                                            // DJNZ
        const int8_t e = int8_t(fetch8());
        if (--hi(bc)) { pc = wz = uint16_t(pc + e); return 13; }
        return 8;
    }
    case 0x18: { const int8_t e = int8_t(fetch8()); pc = wz = uint16_t(pc + e); return 12; }
    case 0x20: case 0x28: case 0x30: case 0x38: {                           // JR cc
        const int8_t e = int8_t(fetch8());
        if (cond(y - 4)) { pc = wz = uint16_t(pc + e); return 12; }
        return 7;
    }
    case 0x22: { const uint16_t a = fetch16(); wr16(a, xy); wz = uint16_t(a + 1); return 16; }
    case 0x2A: { const uint16_t a = fetch16(); xy = rd16(a); wz = uint16_t(a + 1); return 16; }
    case 0x32: {
        const uint16_t a = fetch16();
        write(ctx, a, A);
        wz = uint16_t((A << 8) | ((a + 1) & 0xFF));
        return 13;
    }
    case 0x3A: { const uint16_t a = fetch16(); A = read(ctx, a); wz = uint16_t(a + 1); return 13; }
    case 0x27: daa(); return 4;
    case 0x2F:                                                              // CPL
        A = uint8_t(~A);
        F = uint8_t((F & (SF | ZF | PF | CF)) | HF | NF | (A & (YF | XF)));
        return 4;
    case 0x37: F = uint8_t((F & (SF | ZF | PF)) | CF | (A & (YF | XF))); return 4;   // SCF
    case 0x3F:                                                              // CCF: H = old C
        F = uint8_t((F & (SF | ZF | PF)) | ((F & CF) << 4) | ((F & CF) ^ CF) | (A & (YF | XF)));
        return 4;

    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond(y)) { pc = wz = pop(); return 11; }
        return 5;
    case 0xC1: case 0xD1: case 0xE1: rp(p, xy) = pop(); return 10;
    case 0xF1: af = pop(); return 10;
    case 0xC5: case 0xD5: case 0xE5: push(rp(p, xy)); return 11;
    case 0xF5: push(af); return 11;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
        wz = fetch16();
        if (cond(y)) pc = wz;
        return 10;
    case 0xC3: pc = wz = fetch16(); return 10;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
        wz = fetch16();
        if (cond(y)) { push(pc); pc = wz; return 17; }
        return 10;
    case 0xCD: wz = fetch16(); push(pc); pc = wz; return 17;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(y, fetch8());
        return 7;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(pc);
        pc = wz = uint16_t(op & 0x38);
        return 11;
    case 0xC9: pc = wz = pop(); return 10;
    case 0xD3: {                                                            // OUT (n),A
        const uint8_t n = fetch8();
        port_out(uint16_t((A << 8) | n), A);
        wz = uint16_t((A << 8) | ((n + 1) & 0xFF));
        return 11;
    }
    case 0xDB: {                                                            // IN A,(n)
        const uint16_t port = uint16_t((A << 8) | fetch8());
        A = port_in(port);
        wz = uint16_t(port + 1);
        return 11;
    }
    case 0xD9: std::swap(bc, bc2); std::swap(de, de2); std::swap(hl, hl2); return 4;
    case 0xE3: { const uint16_t v = rd16(sp); wr16(sp, xy); xy = wz = v; return 19; }
    case 0xE9: pc = xy; return 4;
    case 0xEB: std::swap(de, hl); return 4;                                 // never IX/IY
    case 0xF3: iff1 = iff2 = false; return 4;
    case 0xF9: sp = xy; return 6;
    case 0xFB: iff1 = iff2 = true; ei_delay = 1; return 4;
    default: return 4;                                                      // CB/DD/ED/FD: dispatched by step()
    }
}

int Z80::exec_cb() {
    const uint8_t op = fetch_op();
    const int y = (op >> 3) & 7, z = op & 7;
    if (z == 6) {
        uint8_t v = read(ctx, hl);
        switch (op >> 6) {
        case 0:  v = rot(y, v); break;
        case 1:  bit(y, v, uint8_t(wz >> 8)); return 12;
        case 2:  v &= uint8_t(~(1 << y)); break;
        default: v |= uint8_t(1 << y); break;
        }
        write(ctx, hl, v);
        return 15;
    }
    uint8_t& reg = r8(z, hl);
    switch (op >> 6) {
    case 0:  reg = rot(y, reg); break;
    case 1:  bit(y, reg, reg); break;
    case 2:  reg &= uint8_t(~(1 << y)); break;
    default: reg |= uint8_t(1 << y); break;
    }
    return 8;
}

// DD CB d op / FD CB d op. The opcode byte is read as data (no M1), so R only
// counts the two prefixes. Non-BIT forms with a register field also copy the
// result into that (real, not IXH/IXL) register.
int Z80::exec_xycb(uint16_t xy) {
    const uint16_t a = wz = uint16_t(xy + int8_t(fetch8()));
    const uint8_t op = fetch8();
    const int y = (op >> 3) & 7, z = op & 7;
    uint8_t v = read(ctx, a);
    switch (op >> 6) {
    case 0:  v = rot(y, v); break;
    case 1:  bit(y, v, uint8_t(a >> 8)); return 16;
    case 2:  v &= uint8_t(~(1 << y)); break;
    default: v |= uint8_t(1 << y); break;
    }
    write(ctx, a, v);
    if (z != 6) r8(z, hl) = v;
    return 19;
}

int Z80::exec_ed() {
    uint8_t& A = hi(af);
    uint8_t& F = lo(af);
    const uint8_t op = fetch_op();
    const int y = (op >> 3) & 7;

    if ((op & 0xE4) == 0xA0) return block(op);         // A0-A3, A8-AB, B0-B3, B8-BB
    if ((op & 0xC0) != 0x40) return 8;                  // undefined: 8 T-state NOP

    switch (op & 7) {
    case 0: {                                           // IN r,(C); ED 70 only sets flags
        const uint8_t v = port_in(bc);
        wz = uint16_t(bc + 1);
        F = uint8_t((F & CF) | SZP(v));
        if (y != 6) r8(y, hl) = v;
        return 12;
    }
    case 1:                                             // OUT (C),r; ED 71 outputs 0 (NMOS)
        port_out(bc, y == 6 ? 0 : r8(y, hl));
        wz = uint16_t(bc + 1);
        return 12;
    case 2: adc16(rp(y >> 1, hl), !(y & 1)); return 15;    // SBC/ADC HL,rr
    case 3: {                                           // LD (nn),rr / LD rr,(nn)
        const uint16_t a = fetch16();
        if (y & 1) rp(y >> 1, hl) = rd16(a);
        else       wr16(a, rp(y >> 1, hl));
        wz = uint16_t(a + 1);
        return 20;
    }
    case 4: { const uint8_t v = A; A = 0; alu(2, v); return 8; }            // NEG
    case 5: iff1 = iff2; pc = wz = pop(); return 14;                        // RETN/RETI
    case 6: {
        static const uint8_t modes[8] = {0, 0, 1, 2, 0, 0, 1, 2};
        im = modes[y];
        return 8;
    }
    default:
        switch (y) {
        case 0: i = A; return 9;
        case 1: r = A; return 9;
        case 2: case 3:                                 // LD A,I / LD A,R: P/V = IFF2
            A = y == 2 ? i : r;
            F = uint8_t((F & CF) | SZ(A) | (iff2 ? PF : 0));
            return 9;
        case 4: case 5: {                               // RRD / RLD
            const uint8_t v = read(ctx, hl);
            if (y == 4) {
                write(ctx, hl, uint8_t((A << 4) | (v >> 4)));
                A = uint8_t((A & 0xF0) | (v & 0x0F));
            } else {
                write(ctx, hl, uint8_t((v << 4) | (A & 0x0F)));
                A = uint8_t((A & 0xF0) | (v >> 4));
            }
            F = uint8_t((F & CF) | SZP(A));
            wz = uint16_t(hl + 1);
            return 18;
        }
        default: return 8;
        }
    }
}

// Block transfer/compare/I-O. Bit 3 of the opcode selects decrement, bit 4 repeat.
int Z80::block(uint8_t op) {
    uint8_t& A = hi(af);
    uint8_t& F = lo(af);
    uint8_t& B = hi(bc);
    const uint16_t d = (op & 0x08) ? 0xFFFF : 1;
    const bool repeat = (op & 0x10) != 0;
    switch (op & 3) {
    case 0: {                                           // LDI/LDD/LDIR/LDDR
        const uint8_t v = read(ctx, hl);
        write(ctx, de, v);
        hl = uint16_t(hl + d);
        de = uint16_t(de + d);
        bc--;
        // X/Y are bits 3 and 1 of A + the transferred byte.
        const uint8_t n = uint8_t(v + A);
        F = uint8_t((F & (SF | ZF | CF)) | (bc ? PF : 0) | (n & XF) | ((n << 4) & YF));
        if (!(repeat && bc)) return 16;
        break;
    }
    case 1: {                                           // CPI/CPD/CPIR/CPDR
        const uint8_t v = read(ctx, hl);
        const uint8_t res = uint8_t(A - v);
        const uint8_t h = (A ^ v ^ res) & HF;
        // X/Y are bits 3 and 1 of A - (HL) - H.
        const uint8_t n = uint8_t(res - (h >> 4));
        hl = uint16_t(hl + d);
        wz = uint16_t(wz + d);
        bc--;
        F = uint8_t((F & CF) | NF | (SZ(res) & (SF | ZF)) | h | (bc ? PF : 0) | (n & XF) | ((n << 4) & YF));
        if (!(repeat && bc && res)) return 16;
        break;
    }
    default: {                                          // INI/IND/OUTI/OUTD and repeats
        uint8_t v;
        unsigned k;
        if ((op & 3) == 2) {
            v = port_in(bc);
            wz = uint16_t(bc + d);
            B--;
            write(ctx, hl, v);
            hl = uint16_t(hl + d);
            k = v + uint8_t(lo(bc) + d);
        } else {
            v = read(ctx, hl);
            B--;
            wz = uint16_t(bc + d);
            port_out(bc, v);
            hl = uint16_t(hl + d);
            k = v + lo(hl);
        }
        // S/Z/X/Y from B, N = bit 7 of the byte, H = C = carry of k,
        // P/V = parity of (k & 7) ^ B.
        F = uint8_t(SZ(B) | ((v >> 6) & NF) | (k > 0xFF ? (HF | CF) : 0) | (SZP((k & 7) ^ B) & PF));
        if (!(repeat && B)) return 16;
        pc = uint16_t(pc - 2);
        return 21;
    }
    }
    pc = uint16_t(pc - 2);
    wz = uint16_t(pc + 1);
    return 21;
}

// ---------------------------------------------------------------------------
// Interrupts and run loop

int Z80::take_interrupt() {
    halted = false;
    r = uint8_t((r & 0x80) | ((r + 1) & 0x7F));
    push(pc);
    if (nmi_pending) {
        nmi_pending = false;
        iff1 = false;                                   // IFF2 keeps the pre-NMI state for RETN
        pc = wz = 0x0066;
        return 11;
    }
    iff1 = iff2 = false;
    if (im == 2) {
        // Nothing drives the data bus on the Mega Drive: the vector byte reads 0xFF.
        pc = wz = rd16(uint16_t((i << 8) | 0xFF));
        return 19;
    }
    pc = wz = 0x0038;                                   // IM 1, and IM 0 fed RST 38h
    return 13;
}

int Z80::run(int cycles) {
    run_done = 0;
    if (cycles <= 0) return 0;
    run_active = true;
    run_stop = false;
    do {
        if (nmi_pending || (irq_line && iff1 && !ei_delay)) {
            run_done += take_interrupt();
        } else if (halted) {
            // Nothing can change until run() returns: burn the rest as 4 T-state NOPs.
            const int n = (cycles - run_done + 3) >> 2;
            r = uint8_t((r & 0x80) | ((r + n) & 0x7F));
            run_done += n * 4;
        } else {
            ei_delay = 0;
            run_done += step();
        }
        run_done += wait_cycles;
        wait_cycles = 0;
    } while (run_done < cycles && !run_stop);
    run_active = false;
    return run_done;
}

void Z80::reset() {
    pc = 0;
    i = r = 0;
    im = 0;
    iff1 = iff2 = false;
    halted = false;
    nmi_pending = false;
    af = sp = 0xFFFF;
    wz = 0;
    ei_delay = 0;
}

// ---------------------------------------------------------------------------
// Save states

size_t Z80::state_size() const { return sizeof(State); }

void Z80::save_state(uint8_t* dst) const {
    State s;
    std::memset(&s, 0, sizeof s);   // deterministic padding
    s.af = af; s.bc = bc; s.de = de; s.hl = hl;
    s.af2 = af2; s.bc2 = bc2; s.de2 = de2; s.hl2 = hl2;
    s.ix = ix; s.iy = iy; s.sp = sp; s.pc = pc; s.wz = wz;
    s.i = i; s.r = r; s.im = im;
    s.iff1 = iff1; s.iff2 = iff2; s.halted = halted;
    s.irq_line = irq_line; s.nmi_pending = nmi_pending;
    s.ei_delay = ei_delay; s.wait_cycles = wait_cycles;
    std::memcpy(dst, &s, sizeof s);
}

void Z80::load_state(const uint8_t* src) {
    State s;
    std::memcpy(&s, src, sizeof s);
    af = s.af; bc = s.bc; de = s.de; hl = s.hl;
    af2 = s.af2; bc2 = s.bc2; de2 = s.de2; hl2 = s.hl2;
    ix = s.ix; iy = s.iy; sp = s.sp; pc = s.pc; wz = s.wz;
    i = s.i; r = s.r; im = s.im;
    iff1 = s.iff1 != 0; iff2 = s.iff2 != 0; halted = s.halted != 0;
    irq_line = s.irq_line != 0; nmi_pending = s.nmi_pending != 0;
    ei_delay = s.ei_delay; wait_cycles = s.wait_cycles;
}
