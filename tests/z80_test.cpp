// Self-test for the Z80 core (src/core/z80.cpp). Hand-assembled programs run
// from a flat 64 KB RAM. 8-bit ALU/rotate/DAA flags are compared exhaustively
// against an independent reference model; the rest checks block instructions,
// indexing, stack, T-state tables, interrupts, run control and save states.
// Prints PASS/FAIL per section; exit code 0 when everything passes.
#include "../src/core/z80.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

const uint8_t S = 0x80, Z = 0x40, Y = 0x20, H = 0x10, X = 0x08, V = 0x04, N = 0x02, C = 0x01;

// ---------------------------------------------------------------------------
// Test machine

uint8_t mem[0x10000];
Z80 cpu;
int high_wait = 0;                        // wait states added per read of 8000-FFFF
std::vector<int> stamps;                  // executed() seen by writes to 7F00
std::vector<std::pair<uint16_t, uint8_t>> out_log;
std::vector<uint16_t> in_log;
uint8_t in_data[256];
int in_pos = 0;

uint8_t bus_read(void* ctx, uint16_t a) {
    if (a >= 0x8000) static_cast<Z80*>(ctx)->wait_cycles += high_wait;
    return mem[a];
}

void bus_write(void* ctx, uint16_t a, uint8_t v) {
    Z80* z = static_cast<Z80*>(ctx);
    if (a == 0x7F00) stamps.push_back(z->executed());
    if (a == 0x7F01) z->end_run();
    mem[a] = v;
}

uint8_t io_in(void*, uint16_t port) {
    in_log.push_back(port);
    return in_data[in_pos++ & 255];
}

void io_out(void*, uint16_t port, uint8_t v) { out_log.emplace_back(port, v); }

void load(uint16_t at, std::initializer_list<int> bytes) {
    for (int b : bytes) mem[at++] = uint8_t(b);
}

// Clear RAM, load `code` at `org`, reset the CPU and point PC there (SP = F000).
void fresh(uint16_t org, std::initializer_list<int> code) {
    std::memset(mem, 0, sizeof mem);
    load(org, code);
    cpu = Z80();
    cpu.ctx = &cpu;
    cpu.read = bus_read;
    cpu.write = bus_write;
    cpu.in = io_in;
    cpu.out = io_out;
    cpu.reset();
    cpu.pc = org;
    cpu.sp = 0xF000;
    high_wait = 0;
    stamps.clear();
    out_log.clear();
    in_log.clear();
    in_pos = 0;
}

// Single-step until a HALT has executed; returns T-states including the HALT.
int run_to_halt(int limit = 1 << 24) {
    int t = 0;
    while (!cpu.halted && t < limit) t += cpu.run(1);
    return t;
}

uint8_t A() { return uint8_t(cpu.af >> 8); }
uint8_t F() { return uint8_t(cpu.af); }
uint8_t B() { return uint8_t(cpu.bc >> 8); }

// ---------------------------------------------------------------------------
// Reporting

const char* cur_section = "";
int section_fails = 0, total_fails = 0;
long long total_checks = 0;

void check_at(bool ok, int line, const char* fmt, ...) {
    total_checks++;
    if (ok) return;
    if (section_fails++ < 10) {
        std::printf("    %s (line %d): ", cur_section, line);
        va_list ap;
        va_start(ap, fmt);
        std::vprintf(fmt, ap);
        va_end(ap);
        std::printf("\n");
    }
}
#define CHECK(cond, ...) check_at((cond), __LINE__, __VA_ARGS__)

void section(const char* name, void (*fn)()) {
    cur_section = name;
    section_fails = 0;
    fn();
    if (section_fails) std::printf("[FAIL] %s (%d failures)\n", name, section_fails);
    else               std::printf("[PASS] %s\n", name);
    total_fails += section_fails;
}

// ---------------------------------------------------------------------------
// Reference flag model, written from the documented rules rather than the
// core's bit tricks (signed ranges for overflow, nibble sums for half carry).

struct AF { uint8_t a, f; };

uint8_t xyf(unsigned v) { return uint8_t(v & (X | Y)); }
uint8_t szf(uint8_t v) { return uint8_t((v & 0x80 ? S : 0) | (v == 0 ? Z : 0)); }
bool even_parity(uint8_t v) {
    int n = 0;
    for (int b = 0; b < 8; b++) n += (v >> b) & 1;
    return (n & 1) == 0;
}
uint8_t szxyp(uint8_t v) { return uint8_t(szf(v) | xyf(v) | (even_parity(v) ? V : 0)); }

const char* const kAluNames[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};

AF ref_alu(int op, uint8_t a, uint8_t v, int cin) {
    if (op >= 4 && op <= 6) {
        const uint8_t res = uint8_t(op == 4 ? (a & v) : op == 5 ? (a ^ v) : (a | v));
        return {res, uint8_t(szxyp(res) | (op == 4 ? H : 0))};
    }
    const bool sub = op >= 2;
    const int c = (op == 1 || op == 3) ? cin : 0;
    int ures, sres, nib;
    if (sub) {
        ures = a - v - c;
        sres = int8_t(a) - int8_t(v) - c;
        nib = (a & 15) - (v & 15) - c;
    } else {
        ures = a + v + c;
        sres = int8_t(a) + int8_t(v) + c;
        nib = (a & 15) + (v & 15) + c;
    }
    const uint8_t res = uint8_t(ures);
    uint8_t f = uint8_t(szf(res) | xyf(op == 7 ? v : res));   // CP: X/Y from the operand
    if (sub ? nib < 0 : nib > 15) f |= H;
    if (sres < -128 || sres > 127) f |= V;
    if (sub) f |= N;
    if (ures < 0 || ures > 255) f |= C;
    return {op == 7 ? a : res, f};
}

AF ref_inc(uint8_t v, uint8_t fin) {
    const uint8_t res = uint8_t(v + 1);
    return {res, uint8_t((fin & C) | szf(res) | xyf(res) | ((v & 15) + 1 > 15 ? H : 0) | (v == 0x7F ? V : 0))};
}

AF ref_dec(uint8_t v, uint8_t fin) {
    const uint8_t res = uint8_t(v - 1);
    return {res, uint8_t((fin & C) | N | szf(res) | xyf(res) | ((v & 15) - 1 < 0 ? H : 0) | (v == 0x80 ? V : 0))};
}

// DAA from the tables in "The Undocumented Z80 Documented", section 4.7.
AF ref_daa(uint8_t a, uint8_t fin) {
    const bool c = (fin & C) != 0, h = (fin & H) != 0, n = (fin & N) != 0;
    const int hi = a >> 4, lo = a & 15;
    int diff;
    if (!c) {
        if (lo <= 9) diff = hi <= 9 ? (h ? 0x06 : 0x00) : (h ? 0x66 : 0x60);
        else         diff = hi <= 8 ? 0x06 : 0x66;
    } else {
        diff = (lo <= 9 && !h) ? 0x60 : 0x66;
    }
    const bool c2 = c || (lo <= 9 ? hi >= 10 : hi >= 9);
    const bool h2 = n ? (h && lo <= 5) : lo >= 10;
    const uint8_t res = uint8_t(n ? a - diff : a + diff);
    return {res, uint8_t(szxyp(res) | (h2 ? H : 0) | (n ? N : 0) | (c2 ? C : 0))};
}

// CB rotates/shifts: RLC RRC RL RR SLA SRA SLL SRL.
AF ref_rot(int op, uint8_t v, int cin) {
    int bit_out, res;
    switch (op) {
    case 0:  bit_out = v >> 7; res = (v << 1) | bit_out; break;
    case 1:  bit_out = v & 1;  res = (v >> 1) | (bit_out << 7); break;
    case 2:  bit_out = v >> 7; res = (v << 1) | cin; break;
    case 3:  bit_out = v & 1;  res = (v >> 1) | (cin << 7); break;
    case 4:  bit_out = v >> 7; res = v << 1; break;
    case 5:  bit_out = v & 1;  res = (v >> 1) | (v & 0x80); break;
    case 6:  bit_out = v >> 7; res = (v << 1) | 1; break;
    default: bit_out = v & 1;  res = v >> 1; break;
    }
    const uint8_t r8 = uint8_t(res);
    return {r8, uint8_t(szxyp(r8) | (bit_out ? C : 0))};
}

// INI/IND/OUTI/OUTD flags (Sean Young): S/Z/X/Y from B, N = bit 7 of the
// byte, H = C = (k > 255), P/V = parity((k & 7) ^ B).
uint8_t ref_io_flags(uint8_t v, unsigned k, uint8_t b) {
    uint8_t f = uint8_t(szf(b) | xyf(b));
    if (v & 0x80) f |= N;
    if (k > 255) f |= H | C;
    if (even_parity(uint8_t((k & 7) ^ b))) f |= V;
    return f;
}

bool cc_true(int cc, uint8_t f) {
    switch (cc) {
    case 0:  return !(f & Z);
    case 1:  return (f & Z) != 0;
    case 2:  return !(f & C);
    case 3:  return (f & C) != 0;
    case 4:  return !(f & V);
    case 5:  return (f & V) != 0;
    case 6:  return !(f & S);
    default: return (f & S) != 0;
    }
}

// ---------------------------------------------------------------------------
// Exhaustive flag tests

void test_alu8() {
    fresh(0x1000, {});
    for (int op = 0; op < 8; op++) {
        mem[0x1000] = uint8_t(0x80 | (op << 3));            // <op> A,B
        for (int a = 0; a < 256; a++)
            for (int v = 0; v < 256; v++)
                for (int cin = 0; cin < 2; cin++) {
                    const uint8_t fin = uint8_t(cin | ((a * 7 + v * 13) & 0xFE));
                    cpu.pc = 0x1000;
                    cpu.af = uint16_t(a << 8 | fin);
                    cpu.bc = uint16_t(v << 8);
                    const int t = cpu.run(1);
                    const AF e = ref_alu(op, uint8_t(a), uint8_t(v), cin);
                    CHECK(t == 4 && A() == e.a && F() == e.f,
                          "%s A=%02X,%02X c=%d: got A=%02X F=%02X t=%d, want A=%02X F=%02X",
                          kAluNames[op], a, v, cin, A(), F(), t, e.a, e.f);
                }
    }
    // Immediate, (HL) and (IX+d) operand forms on a subset.
    for (int op = 0; op < 8; op++)
        for (int a = 0; a < 256; a += 5)
            for (int v = 0; v < 256; v += 3) {
                const AF e = ref_alu(op, uint8_t(a), uint8_t(v), 1);
                load(0x1000, {0xC6 | op << 3, v});
                cpu.pc = 0x1000;
                cpu.af = uint16_t(a << 8 | C);
                int t = cpu.run(1);
                CHECK(t == 7 && A() == e.a && F() == e.f && cpu.pc == 0x1002, "%s n A=%02X,%02X", kAluNames[op], a, v);

                mem[0x3000] = uint8_t(v);
                load(0x1000, {0x86 | op << 3});
                cpu.pc = 0x1000;
                cpu.hl = 0x3000;
                cpu.af = uint16_t(a << 8 | C);
                t = cpu.run(1);
                CHECK(t == 7 && A() == e.a && F() == e.f && cpu.pc == 0x1001, "%s (HL) A=%02X,%02X", kAluNames[op], a, v);

                load(0x1000, {0xDD, 0x86 | op << 3, 0xFE});    // <op> (IX-2)
                cpu.pc = 0x1000;
                cpu.ix = 0x3002;
                cpu.af = uint16_t(a << 8 | C);
                t = cpu.run(1);
                CHECK(t == 19 && A() == e.a && F() == e.f && cpu.pc == 0x1003, "%s (IX-2) A=%02X,%02X", kAluNames[op], a, v);
            }
}

void test_incdec() {
    fresh(0x1000, {});
    for (int v = 0; v < 256; v++)
        for (int fin = 0; fin < 256; fin++)
            for (int dec = 0; dec < 2; dec++) {
                mem[0x1000] = dec ? 0x05 : 0x04;             // INC B / DEC B
                cpu.pc = 0x1000;
                cpu.af = uint16_t(0x5A00 | fin);
                cpu.bc = uint16_t(v << 8 | 0x77);
                const int t = cpu.run(1);
                const AF e = dec ? ref_dec(uint8_t(v), uint8_t(fin)) : ref_inc(uint8_t(v), uint8_t(fin));
                CHECK(t == 4 && B() == e.a && F() == e.f && A() == 0x5A && (cpu.bc & 0xFF) == 0x77,
                      "%s %02X F=%02X: got %02X F=%02X, want %02X F=%02X", dec ? "DEC" : "INC", v, fin, B(), F(), e.a, e.f);
            }
    for (int v = 0; v < 256; v++) {
        load(0x1000, {0x34});                                 // INC (HL)
        mem[0x3000] = uint8_t(v);
        cpu.pc = 0x1000; cpu.hl = 0x3000; cpu.af = 0x0001;
        int t = cpu.run(1);
        AF e = ref_inc(uint8_t(v), 0x01);
        CHECK(t == 11 && mem[0x3000] == e.a && F() == e.f, "INC (HL) %02X", v);

        load(0x1000, {0xFD, 0x35, 0x05});                     // DEC (IY+5)
        mem[0x3000] = uint8_t(v);
        cpu.pc = 0x1000; cpu.iy = 0x2FFB; cpu.af = 0x0000;
        t = cpu.run(1);
        e = ref_dec(uint8_t(v), 0x00);
        CHECK(t == 23 && mem[0x3000] == e.a && F() == e.f, "DEC (IY+5) %02X", v);

        load(0x1000, {0xDD, 0x2C});                           // INC IXL
        cpu.pc = 0x1000; cpu.ix = uint16_t(0x1200 | v); cpu.af = 0x0000;
        t = cpu.run(1);
        e = ref_inc(uint8_t(v), 0x00);
        CHECK(t == 8 && cpu.ix == (0x1200 | e.a) && F() == e.f, "INC IXL %02X", v);
    }
}

void test_acc_ops() {
    fresh(0x1000, {});
    for (int a = 0; a < 256; a++)
        for (int fin = 0; fin < 256; fin++) {
            auto exec = [&](std::initializer_list<int> code) {
                load(0x1000, code);
                cpu.pc = 0x1000;
                cpu.af = uint16_t(a << 8 | fin);
                return cpu.run(1);
            };
            const uint8_t ua = uint8_t(a), uf = uint8_t(fin);

            int t = exec({0x27});                             // DAA
            AF e = ref_daa(ua, uf);
            CHECK(t == 4 && A() == e.a && F() == e.f, "DAA A=%02X F=%02X: got %02X %02X want %02X %02X",
                  a, fin, A(), F(), e.a, e.f);

            t = exec({0x2F});                                 // CPL
            const uint8_t na = uint8_t(~a);
            CHECK(t == 4 && A() == na && F() == ((uf & (S | Z | V | C)) | H | N | xyf(na)), "CPL %02X %02X", a, fin);

            t = exec({0x37});                                 // SCF
            CHECK(t == 4 && A() == ua && F() == ((uf & (S | Z | V)) | C | xyf(ua)), "SCF %02X %02X", a, fin);

            t = exec({0x3F});                                 // CCF
            CHECK(t == 4 && F() == ((uf & (S | Z | V)) | ((uf & C) ? H : C) | xyf(ua)), "CCF %02X %02X", a, fin);

            t = exec({0xED, 0x44});                           // NEG
            e = ref_alu(2, 0, ua, 0);
            CHECK(t == 8 && A() == e.a && F() == e.f, "NEG %02X", a);

            const int cin = fin & 1;
            const int acc_ops[4] = {0x07, 0x0F, 0x17, 0x1F};  // RLCA RRCA RLA RRA
            const int cb_ops[4] = {0, 1, 2, 3};
            for (int k = 0; k < 4; k++) {
                t = exec({acc_ops[k]});
                e = ref_rot(cb_ops[k], ua, cin);
                const uint8_t want_f = uint8_t((uf & (S | Z | V)) | xyf(e.a) | (e.f & C));
                CHECK(t == 4 && A() == e.a && F() == want_f, "rotate-A %02X on %02X F=%02X", acc_ops[k], a, fin);
            }
        }
}

void test_cb_ops() {
    fresh(0x1000, {});
    for (int op = 0; op < 8; op++)
        for (int v = 0; v < 256; v++)
            for (int cin = 0; cin < 2; cin++) {
                const AF e = ref_rot(op, uint8_t(v), cin);
                load(0x1000, {0xCB, op << 3});                // <rot> B
                cpu.pc = 0x1000;
                cpu.af = uint16_t(0x3300 | cin | 0xD6);
                cpu.bc = uint16_t(v << 8);
                int t = cpu.run(1);
                CHECK(t == 8 && B() == e.a && F() == e.f && A() == 0x33, "CB %02X on %02X c=%d", op << 3, v, cin);

                load(0x1000, {0xCB, op << 3 | 6});            // <rot> (HL)
                mem[0x3000] = uint8_t(v);
                cpu.pc = 0x1000; cpu.hl = 0x3000;
                cpu.af = uint16_t(cin);
                t = cpu.run(1);
                CHECK(t == 15 && mem[0x3000] == e.a && F() == e.f, "CB %02X (HL) on %02X", op << 3 | 6, v);
            }
    // BIT n,A: X/Y from the operand; S only for bit 7; P/V mirrors Z.
    for (int n = 0; n < 8; n++)
        for (int v = 0; v < 256; v++)
            for (int fin = 0; fin < 256; fin += 0x55) {
                load(0x1000, {0xCB, 0x47 | n << 3});
                cpu.pc = 0x1000;
                cpu.af = uint16_t(v << 8 | fin);
                const int t = cpu.run(1);
                const bool set = (v >> n) & 1;
                const uint8_t want = uint8_t((fin & C) | H | xyf(v) | (set ? (n == 7 ? S : 0) : (Z | V)));
                CHECK(t == 8 && F() == want && A() == v, "BIT %d,A=%02X F=%02X: got %02X want %02X", n, v, fin, F(), want);
            }
    // BIT n,(HL): X/Y from the high byte of MEMPTR.
    for (int n = 0; n < 8; n++)
        for (int w = 0; w < 256; w += 0x11) {
            load(0x1000, {0xCB, 0x46 | n << 3});
            mem[0x3000] = uint8_t(1 << n);
            cpu.pc = 0x1000; cpu.hl = 0x3000; cpu.af = 0; cpu.wz = uint16_t(w << 8);
            const int t = cpu.run(1);
            CHECK(t == 12 && F() == (H | xyf(w) | (n == 7 ? S : 0)), "BIT %d,(HL) wz=%02X00", n, w);
        }
    // SET / RES on a register and on (HL).
    for (int n = 0; n < 8; n++) {
        load(0x1000, {0xCB, 0xC2 | n << 3});                  // SET n,D
        cpu.pc = 0x1000; cpu.de = 0x0000; cpu.af = 0x00AA;
        int t = cpu.run(1);
        CHECK(t == 8 && cpu.de == (1 << n) << 8 && F() == 0xAA, "SET %d,D", n);
        load(0x1000, {0xCB, 0x86 | n << 3});                  // RES n,(HL)
        mem[0x3000] = 0xFF; cpu.pc = 0x1000; cpu.hl = 0x3000;
        t = cpu.run(1);
        CHECK(t == 15 && mem[0x3000] == (0xFF & ~(1 << n)), "RES %d,(HL)", n);
    }
}

void test_arith16() {
    fresh(0x1000, {});
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return uint16_t(seed >> 12); };
    static const uint16_t edge[] = {0x0000, 0x0001, 0x0FFF, 0x1000, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0xF000};
    for (int i = 0; i < 200000; i++) {
        uint16_t a = rnd(), b = rnd();
        const uint8_t fin = uint8_t(rnd());
        if (i < 81) { a = edge[i / 9]; b = edge[i % 9]; }
        const int c = fin & 1;

        // ADD HL,DE / ADD IX,DE: S, Z, P/V kept; H from bit 11; X/Y from the high byte.
        const unsigned sum = unsigned(a) + b;
        const uint8_t add_f = uint8_t((fin & (S | Z | V)) | xyf(sum >> 8) |
                                      (((a & 0xFFF) + (b & 0xFFF)) > 0xFFF ? H : 0) | (sum > 0xFFFF ? C : 0));
        load(0x1000, {0x19});
        cpu.pc = 0x1000; cpu.hl = a; cpu.de = b; cpu.af = fin;
        int t = cpu.run(1);
        CHECK(t == 11 && cpu.hl == uint16_t(sum) && F() == add_f && cpu.wz == uint16_t(a + 1), "ADD HL %04X+%04X", a, b);
        load(0x1000, {0xDD, 0x19});
        cpu.pc = 0x1000; cpu.ix = a; cpu.de = b; cpu.af = fin;
        t = cpu.run(1);
        CHECK(t == 15 && cpu.ix == uint16_t(sum) && F() == add_f, "ADD IX %04X+%04X", a, b);

        // ADC HL,DE
        const long ures = long(a) + b + c;
        const long sres = long(int16_t(a)) + int16_t(b) + c;
        const uint16_t r16 = uint16_t(ures);
        const uint8_t adc_f = uint8_t((r16 & 0x8000 ? S : 0) | (r16 == 0 ? Z : 0) | xyf(r16 >> 8) |
                                      (((a & 0xFFF) + (b & 0xFFF) + c) > 0xFFF ? H : 0) |
                                      (sres < -32768 || sres > 32767 ? V : 0) | (ures > 0xFFFF ? C : 0));
        load(0x1000, {0xED, 0x5A});
        cpu.pc = 0x1000; cpu.hl = a; cpu.de = b; cpu.af = fin;
        t = cpu.run(1);
        CHECK(t == 15 && cpu.hl == r16 && F() == adc_f, "ADC HL %04X+%04X+%d: F=%02X want %02X", a, b, c, F(), adc_f);

        // SBC HL,DE
        const long dres = long(a) - b - c;
        const long sdres = long(int16_t(a)) - int16_t(b) - c;
        const uint16_t d16 = uint16_t(dres);
        const uint8_t sbc_f = uint8_t((d16 & 0x8000 ? S : 0) | (d16 == 0 ? Z : 0) | xyf(d16 >> 8) | N |
                                      (int(a & 0xFFF) - int(b & 0xFFF) - c < 0 ? H : 0) |
                                      (sdres < -32768 || sdres > 32767 ? V : 0) | (dres < 0 ? C : 0));
        load(0x1000, {0xED, 0x52});
        cpu.pc = 0x1000; cpu.hl = a; cpu.de = b; cpu.af = fin;
        t = cpu.run(1);
        CHECK(t == 15 && cpu.hl == d16 && F() == sbc_f, "SBC HL %04X-%04X-%d: F=%02X want %02X", a, b, c, F(), sbc_f);
    }
    // SBC HL,HL with carry: FFFF, S/H/N/C set.
    load(0x1000, {0xED, 0x62});
    cpu.pc = 0x1000; cpu.hl = 0x1234; cpu.af = 0x0001;
    cpu.run(1);
    CHECK(cpu.hl == 0xFFFF && F() == (S | Y | H | X | N | C), "SBC HL,HL F=%02X", F());
}

// ---------------------------------------------------------------------------
// Programs

void test_block() {
    // LDIR: 10 bytes 4000 -> 5000.
    fresh(0, {0x21, 0x00, 0x40, 0x11, 0x00, 0x50, 0x01, 0x0A, 0x00, 0xED, 0xB0, 0x76});
    for (int k = 0; k < 10; k++) mem[0x4000 + k] = uint8_t(0xA0 + k);
    mem[0x500A] = 0x77;
    int t = run_to_halt();
    bool same = true;
    for (int k = 0; k < 10; k++) same &= mem[0x5000 + k] == 0xA0 + k;
    CHECK(t == 30 + 9 * 21 + 16 + 4, "LDIR T=%d", t);
    CHECK(same && mem[0x500A] == 0x77, "LDIR data");
    CHECK(cpu.hl == 0x400A && cpu.de == 0x500A && cpu.bc == 0 && !(F() & V), "LDIR regs HL=%04X DE=%04X", cpu.hl, cpu.de);
    CHECK(cpu.r == 3 + 10 * 2 + 1, "LDIR R=%d", cpu.r);

    // LDDR: overlapping move of 4000-400F up by one byte.
    fresh(0, {0x21, 0x0F, 0x40, 0x11, 0x10, 0x40, 0x01, 0x10, 0x00, 0xED, 0xB8, 0x76});
    for (int k = 0; k < 16; k++) mem[0x4000 + k] = uint8_t(k + 1);
    t = run_to_halt();
    same = mem[0x4000] == 1;
    for (int k = 0; k < 16; k++) same &= mem[0x4001 + k] == k + 1;
    CHECK(t == 30 + 15 * 21 + 16 + 4 && same, "LDDR T=%d", t);
    CHECK(cpu.hl == 0x3FFF && cpu.de == 0x4000 && cpu.bc == 0, "LDDR regs");

    // LDI / CPD flags against the reference rules.
    fresh(0x1000, {});
    for (int a = 0; a < 256; a++)
        for (int v = 0; v < 256; v += 7)
            for (int bc0 = 1; bc0 <= 2; bc0++) {
                const uint8_t fin = uint8_t(a ^ 0xC5);
                load(0x1000, {0xED, 0xA0});                   // LDI
                mem[0x4000] = uint8_t(v);
                cpu.pc = 0x1000; cpu.hl = 0x4000; cpu.de = 0x5000; cpu.bc = uint16_t(bc0);
                cpu.af = uint16_t(a << 8 | fin);
                t = cpu.run(1);
                const uint8_t n = uint8_t(a + v);
                uint8_t want = uint8_t((fin & (S | Z | C)) | (bc0 == 2 ? V : 0) | (n & 0x08 ? X : 0) | (n & 0x02 ? Y : 0));
                CHECK(t == 16 && F() == want && mem[0x5000] == v && cpu.hl == 0x4001 && cpu.de == 0x5001,
                      "LDI A=%02X v=%02X F=%02X want %02X", a, v, F(), want);

                load(0x1000, {0xED, 0xA9});                   // CPD
                cpu.pc = 0x1000; cpu.hl = 0x4000; cpu.bc = uint16_t(bc0);
                cpu.af = uint16_t(a << 8 | fin);
                t = cpu.run(1);
                const uint8_t res = uint8_t(a - v);
                const bool h = (a & 15) < (v & 15);
                const uint8_t k = uint8_t(res - (h ? 1 : 0));
                want = uint8_t((fin & C) | N | szf(res) | (h ? H : 0) | (bc0 == 2 ? V : 0) | (k & 0x08 ? X : 0) | (k & 0x02 ? Y : 0));
                CHECK(t == 16 && F() == want && A() == a && cpu.hl == 0x3FFF, "CPD A=%02X v=%02X F=%02X want %02X", a, v, F(), want);
            }

    // CPIR: found in the middle, found at the last byte, not found.
    struct { char target; int t; uint16_t hl, bc; bool z, pv; } cps[] = {
        {'W', 27 + 6 * 21 + 16 + 4, 0x4007, 4, true, true},
        {'D', 27 + 10 * 21 + 16 + 4, 0x400B, 0, true, false},
        {'Q', 27 + 10 * 21 + 16 + 4, 0x400B, 0, false, false},
    };
    for (const auto& cp : cps) {
        fresh(0, {0x21, 0x00, 0x40, 0x01, 0x0B, 0x00, 0x3E, cp.target, 0xED, 0xB1, 0x76});
        std::memcpy(mem + 0x4000, "HELLO WORLD", 11);
        t = run_to_halt();
        CHECK(t == cp.t && cpu.hl == cp.hl && cpu.bc == cp.bc && ((F() & Z) != 0) == cp.z && ((F() & V) != 0) == cp.pv,
              "CPIR '%c' T=%d HL=%04X BC=%04X F=%02X", cp.target, t, cpu.hl, cpu.bc, F());
    }
    // CPDR searching backwards.
    fresh(0, {0x21, 0x0A, 0x40, 0x01, 0x0B, 0x00, 0x3E, 'L', 0xED, 0xB9, 0x76});
    std::memcpy(mem + 0x4000, "HELLO WORLD", 11);
    t = run_to_halt();
    CHECK(t == 27 + 21 + 16 + 4 && cpu.hl == 0x4008 && cpu.bc == 9 && (F() & Z), "CPDR T=%d HL=%04X", t, cpu.hl);

    // OTIR: port is BC after B is decremented.
    fresh(0, {0x21, 0x00, 0x40, 0x01, 0x10, 0x03, 0xED, 0xB3, 0x76});
    load(0x4000, {0x11, 0x22, 0x33});
    t = run_to_halt();
    CHECK(t == 20 + 2 * 21 + 16 + 4, "OTIR T=%d", t);
    CHECK(out_log.size() == 3 && out_log[0] == std::make_pair(uint16_t(0x0210), uint8_t(0x11)) &&
          out_log[1] == std::make_pair(uint16_t(0x0110), uint8_t(0x22)) &&
          out_log[2] == std::make_pair(uint16_t(0x0010), uint8_t(0x33)), "OTIR output");
    CHECK(B() == 0 && (F() & Z) && cpu.hl == 0x4003, "OTIR regs");

    // INIR: port is BC before B is decremented.
    fresh(0, {0x21, 0x00, 0x40, 0x01, 0x20, 0x03, 0xED, 0xB2, 0x76});
    in_data[0] = 0xA1; in_data[1] = 0xA2; in_data[2] = 0xA3;
    t = run_to_halt();
    CHECK(t == 20 + 2 * 21 + 16 + 4, "INIR T=%d", t);
    CHECK(in_log.size() == 3 && in_log[0] == 0x0320 && in_log[1] == 0x0220 && in_log[2] == 0x0120, "INIR ports");
    CHECK(mem[0x4000] == 0xA1 && mem[0x4001] == 0xA2 && mem[0x4002] == 0xA3 && B() == 0 && (F() & Z), "INIR data");

    // INI/IND/OUTI/OUTD flags.
    static const uint8_t cs[] = {0x00, 0x10, 0x7F, 0xFF};
    static const uint8_t bs[] = {0x00, 0x01, 0x02, 0x80, 0x29};
    static const uint16_t hls[] = {0x4000, 0x40FF, 0x4080};
    fresh(0x1000, {});
    for (int v = 0; v < 256; v++)
        for (uint8_t c : cs)
            for (uint8_t b : bs)
                for (uint16_t hl0 : hls) {
                    const uint8_t b1 = uint8_t(b - 1);
                    for (int kind = 0; kind < 4; kind++) {
                        static const int ops[4] = {0xA2, 0xAA, 0xA3, 0xAB};   // INI IND OUTI OUTD
                        const int dir = (kind & 1) ? -1 : 1;
                        load(0x1000, {0xED, ops[kind]});
                        mem[hl0] = uint8_t(v);
                        in_data[in_pos & 255] = uint8_t(v);
                        cpu.pc = 0x1000; cpu.bc = uint16_t(b << 8 | c); cpu.hl = hl0; cpu.af = 0;
                        const int t1 = cpu.run(1);
                        const unsigned k = kind < 2 ? v + uint8_t(c + dir) : v + uint8_t(hl0 + dir);
                        const uint8_t want = ref_io_flags(uint8_t(v), k, b1);
                        CHECK(t1 == 16 && F() == want && B() == b1 && cpu.hl == uint16_t(hl0 + dir) && mem[hl0] == v,
                              "ED %02X v=%02X B=%02X C=%02X HL=%04X: F=%02X want %02X", ops[kind], v, b, c, hl0, F(), want);
                    }
                }
}

void test_stack() {
    fresh(0, {
        0x31, 0x00, 0xF0,       // LD SP,F000     10
        0x01, 0x34, 0x12,       // LD BC,1234     10
        0xCD, 0x10, 0x00,       // CALL 0010      17
        0x76,                   // HALT           4
    });
    load(0x0010, {0xC5, 0xD1, 0xC9});                          // PUSH BC; POP DE; RET
    int t = run_to_halt();
    CHECK(t == 72, "call/ret T=%d", t);
    CHECK(cpu.de == 0x1234 && cpu.sp == 0xF000 && cpu.pc == 0x000A, "call/ret regs DE=%04X SP=%04X PC=%04X", cpu.de, cpu.sp, cpu.pc);
    CHECK(mem[0xEFFE] == 0x09 && mem[0xEFFF] == 0x00 && mem[0xEFFC] == 0x34 && mem[0xEFFD] == 0x12, "stack contents");

    fresh(0, {
        0x31, 0x00, 0xF0,       // 0000 LD SP,F000     10
        0xAF,                   // 0003 XOR A          4   Z=1
        0xC4, 0x20, 0x00,       // 0004 CALL NZ,0020   10  not taken
        0xCC, 0x20, 0x00,       // 0007 CALL Z,0020    17
        0x21, 0xCD, 0xAB,       // 000A LD HL,ABCD     10
        0xE5,                   // 000D PUSH HL        11
        0x21, 0x00, 0x00,       // 000E LD HL,0        10
        0xE3,                   // 0011 EX (SP),HL     19
        0xC1,                   // 0012 POP BC         10
        0xF7,                   // 0013 RST 30h        11
        0x76,                   // 0014 HALT           4
    });
    load(0x0020, {0x3C, 0xC8, 0xC0});                          // INC A; RET Z (5); RET NZ (11)
    load(0x0030, {0x3C, 0xC9});                                // INC A; RET
    t = run_to_halt();
    CHECK(t == 150, "cond call/ret T=%d", t);
    CHECK(A() == 2 && cpu.hl == 0xABCD && cpu.bc == 0 && cpu.sp == 0xF000 && cpu.pc == 0x0015,
          "cond call/ret regs A=%02X HL=%04X BC=%04X SP=%04X PC=%04X", A(), cpu.hl, cpu.bc, cpu.sp, cpu.pc);

    // PUSH AF / POP AF round trip, EX AF,AF', EXX.
    fresh(0, {0xF5, 0x08, 0xF1, 0xD9, 0x76});
    cpu.af = 0x12D7; cpu.af2 = 0x0000;
    cpu.bc = 1; cpu.de = 2; cpu.hl = 3; cpu.bc2 = 4; cpu.de2 = 5; cpu.hl2 = 6;
    run_to_halt();
    CHECK(cpu.af == 0x12D7 && cpu.af2 == 0x12D7 && cpu.bc == 4 && cpu.de == 5 && cpu.hl == 6 &&
          cpu.bc2 == 1 && cpu.de2 == 2 && cpu.hl2 == 3, "PUSH/POP AF, EX AF,AF', EXX");
}

void test_index() {
    fresh(0, {
        0xDD, 0x21, 0x03, 0x28,         // LD IX,2803        14
        0xDD, 0x36, 0xFB, 0x42,         // LD (IX-5),42      19  [27FE]
        0xDD, 0x7E, 0xFB,               // LD A,(IX-5)       19
        0xDD, 0x36, 0x03, 0x01,         // LD (IX+3),01      19  [2806]
        0xDD, 0x86, 0x03,               // ADD A,(IX+3)      19  A=43
        0xDD, 0x34, 0xFF,               // INC (IX-1)        23  [2802]
        0xFD, 0x21, 0x80, 0x60,         // LD IY,6080        14
        0xFD, 0x77, 0x80,               // LD (IY-128),A     19  [6000]
        0xFD, 0x46, 0x80,               // LD B,(IY-128)     19
        0xFD, 0xCB, 0x80, 0xFE,         // SET 7,(IY-128)    23
        0xDD, 0xCB, 0xFB, 0x01,         // RLC (IX-5),C      23  undocumented copy to C
        0xDD, 0xCB, 0xFB, 0x56,         // BIT 2,(IX-5)      20
        0x76,                           // HALT              4
    });
    int t = run_to_halt();
    CHECK(t == 235, "indexed T=%d", t);
    CHECK(A() == 0x43 && B() == 0x43 && mem[0x2806] == 1 && mem[0x2802] == 1 && mem[0x6000] == 0xC3,
          "indexed data A=%02X B=%02X", A(), B());
    CHECK(mem[0x27FE] == 0x84 && (cpu.bc & 0xFF) == 0x84, "DDCB copy-back mem=%02X C=%02X", mem[0x27FE], cpu.bc & 0xFF);
    // BIT: H set, Z clear, C from RLC (0); X/Y from the high byte of IX-5 = 27h.
    CHECK(F() == (H | Y), "BIT (IX+d) F=%02X", F());
    CHECK(cpu.r == 12 * 2 + 1, "indexed R=%d", cpu.r);

    // Undocumented IXH/IXL/IYH forms; (IX+d) forms still use the real H/L.
    fresh(0, {
        0xDD, 0x26, 0x12,               // LD IXH,12         11
        0xDD, 0x2E, 0x34,               // LD IXL,34         11
        0x21, 0x00, 0x50,               // LD HL,5000        10
        0xDD, 0x7C,                     // LD A,IXH          8
        0xDD, 0x85,                     // ADD A,IXL         8   A=46
        0xDD, 0x66, 0x00,               // LD H,(IX+0)       19  H=99
        0xDD, 0x6F,                     // LD IXL,A          8
        0xDD, 0x24,                     // INC IXH           8
        0xFD, 0x21, 0x00, 0x7F,         // LD IY,7F00        14
        0xFD, 0x24,                     // INC IYH           8
        0x76,
    });
    mem[0x1234] = 0x99;
    t = run_to_halt();
    CHECK(t == 109, "IXH/IXL T=%d", t);
    CHECK(cpu.ix == 0x1346 && cpu.hl == 0x9900 && A() == 0x46 && cpu.iy == 0x8000 && F() == (S | H | V),
          "IXH/IXL IX=%04X HL=%04X A=%02X IY=%04X F=%02X", cpu.ix, cpu.hl, A(), cpu.iy, F());

    // DDCB register copy-back uses the real registers, and SET/RES forms too.
    fresh(0, {0xDD, 0xCB, 0x01, 0xC0, 0xDD, 0xCB, 0x01, 0x85, 0x76});   // SET 0,(IX+1),B; RES 0,(IX+1),L
    cpu.ix = 0x3000; cpu.hl = 0xAAAA; mem[0x3001] = 0x10;
    run_to_halt();
    CHECK(B() == 0x11 && cpu.hl == 0xAA10 && cpu.ix == 0x3000 && mem[0x3001] == 0x10, "DDCB SET/RES copy-back");

    // Prefix chains: the last prefix wins, each costs 4 T-states and one R.
    fresh(0, {0xDD, 0xFD, 0x21, 0x34, 0x12});
    cpu.ix = 0xAAAA;
    t = cpu.run(1);
    CHECK(t == 18 && cpu.iy == 0x1234 && cpu.ix == 0xAAAA && cpu.r == 3 && cpu.pc == 5, "DD FD 21 t=%d", t);
    fresh(0, {0xFD, 0xDD, 0x21, 0x34, 0x12});
    t = cpu.run(1);
    CHECK(t == 18 && cpu.ix == 0x1234 && cpu.iy == 0 && cpu.pc == 5, "FD DD 21 t=%d", t);
    fresh(0, {0xDD, 0xDD, 0xDD, 0x00});
    t = cpu.run(1);
    CHECK(t == 16 && cpu.r == 4 && cpu.pc == 4, "DD DD DD 00 t=%d", t);
    fresh(0, {0xDD, 0xED, 0x44});                                        // prefix ignored by ED
    cpu.af = 0x0100;
    t = cpu.run(1);
    CHECK(t == 12 && A() == 0xFF && cpu.r == 3, "DD ED 44 t=%d", t);
    fresh(0, {0xDD, 0xEB});                                              // EX DE,HL ignores the prefix
    cpu.de = 1; cpu.hl = 2; cpu.ix = 3;
    t = cpu.run(1);
    CHECK(t == 8 && cpu.de == 2 && cpu.hl == 1 && cpu.ix == 3, "DD EB");
    fresh(0, {0xFD, 0xE9});                                              // JP (IY)
    cpu.iy = 0x4321;
    t = cpu.run(1);
    CHECK(t == 8 && cpu.pc == 0x4321, "JP (IY)");
    // An endless run of prefixes must not hang run().
    std::memset(mem, 0xDD, sizeof mem);
    cpu.pc = 0;
    t = cpu.run(1000);
    CHECK(t >= 1000 && t < 2000, "prefix flood t=%d", t);
}

void test_djnz() {
    fresh(0, {0x06, 0x0A, 0x10, 0xFE, 0x76});                  // LD B,10; DJNZ $; HALT
    int t = run_to_halt();
    CHECK(t == 7 + 9 * 13 + 8 + 4 && B() == 0 && cpu.pc == 5 && cpu.r == 12, "DJNZ T=%d R=%d", t, cpu.r);

    fresh(0, {
        0x0E, 0x05,             // LD C,5
        0x06, 0x04,             // LD B,4
        0x10, 0xFE,             // DJNZ $
        0x0D,                   // DEC C
        0x20, 0xF9,             // JR NZ,0002
        0x76,
    });
    t = run_to_halt();
    CHECK(t == 7 + 5 * (7 + 47 + 4) + 4 * 12 + 7 + 4, "nested DJNZ T=%d", t);

    // Forward JR / JP sequence with mixed timings.
    fresh(0, {
        0x3E, 0x05,             // 0000 LD A,5          7
        0x18, 0x02,             // 0002 JR +2           12
        0x00, 0x00,
        0x3D,                   // 0006 DEC A           4
        0xC2, 0x06, 0x00,       // 0007 JP NZ,0006      10
        0x28, 0x00,             // 000A JR Z,+0         12
        0x30, 0x00,             // 000C JR NC,+0        7   (C still set from reset)
        0x76,
    });
    t = run_to_halt();
    CHECK(t == 7 + 12 + 5 * (4 + 10) + 12 + 7 + 4 && A() == 0, "JR/JP T=%d", t);
}

void test_interrupts() {
    // IM 1 wake-up from HALT; the pushed address is the one after HALT.
    fresh(0, {0xED, 0x56, 0xFB, 0x76, 0x00, 0x76});           // IM 1; EI; HALT; NOP; HALT
    load(0x38, {0xF5, 0x3A, 0x00, 0x40, 0x3C, 0x32, 0x00, 0x40, 0xF1, 0xFB, 0xED, 0x4D});
    int t = run_to_halt();
    CHECK(t == 16 && cpu.pc == 4 && cpu.iff1 && cpu.im == 1, "IM1 setup T=%d", t);
    const uint8_t r0 = cpu.r;
    t = cpu.run(100);
    CHECK(t == 100 && cpu.halted && cpu.pc == 4 && cpu.r == r0 + 25, "halted run t=%d R=%d", t, cpu.r);
    CHECK(cpu.run(1) == 4, "halted NOP");
    cpu.set_irq(true);
    t = cpu.run(1);
    CHECK(t == 13 && cpu.pc == 0x38 && !cpu.halted && !cpu.iff1 && !cpu.iff2 && cpu.sp == 0xEFFE &&
          mem[0xEFFE] == 0x04 && mem[0xEFFF] == 0x00, "IM1 accept t=%d PC=%04X", t, cpu.pc);
    cpu.set_irq(false);
    t = run_to_halt();
    CHECK(t == 11 + 13 + 4 + 13 + 10 + 4 + 14 + 4 + 4, "ISR T=%d", t);
    CHECK(mem[0x4000] == 1 && cpu.iff1 && cpu.sp == 0xF000 && cpu.pc == 6, "ISR result");

    // EI delay: the instruction after EI runs before the interrupt.
    fresh(0, {0xFB, 0x00, 0x00, 0x00});
    cpu.im = 1;
    cpu.set_irq(true);
    CHECK(cpu.run(1) == 4 && cpu.pc == 1, "EI");
    CHECK(cpu.run(1) == 4 && cpu.pc == 2, "instruction after EI");
    t = cpu.run(1);
    CHECK(t == 13 && cpu.pc == 0x38 && mem[0xEFFE] == 0x02, "IRQ after EI delay t=%d", t);

    // Consecutive EIs keep deferring.
    fresh(0, {0xFB, 0xFB, 0x00, 0x00});
    cpu.im = 1;
    cpu.set_irq(true);
    cpu.run(1); cpu.run(1);
    CHECK(cpu.run(1) == 4 && cpu.pc == 3, "EI; EI; NOP");
    CHECK(cpu.run(1) == 13 && cpu.pc == 0x38, "IRQ after EI; EI; NOP");

    // EI; DI: the DI runs inside the EI shadow, after which the level is ignored.
    fresh(0, {0xFB, 0xF3});
    cpu.im = 1;
    cpu.set_irq(true);
    t = cpu.run(40);
    CHECK(t == 40 && cpu.pc == 10 && !cpu.iff1, "DI ignores IRQ t=%d PC=%04X", t, cpu.pc);

    // Level-triggered: an ISR that re-enables at once is re-entered while /INT stays low.
    fresh(0, {0xED, 0x56, 0xFB, 0x18, 0xFE});                 // IM 1; EI; JR $
    load(0x38, {0x34, 0xFB, 0xED, 0x4D});                     // INC (HL); EI; RETI
    cpu.hl = 0x4000;
    CHECK(cpu.run(12) == 12, "setup");
    cpu.set_irq(true);
    // JR (EI shadow), then 42 T-state rounds of ack+INC+EI+RETI; stops after the 5th INC.
    t = cpu.run(200);
    CHECK(t == 12 + 4 * 42 + 13 + 11 && mem[0x4000] == 5, "level-triggered re-entry t=%d count=%d", t, mem[0x4000]);
    cpu.set_irq(false);
    cpu.run(200);
    CHECK(mem[0x4000] == 5 && cpu.pc >= 3 && cpu.pc <= 4, "IRQ released");

    // NMI: 11 T-states, IFF1 cleared, IFF2 kept; RETN restores IFF1.
    fresh(0, {0xFB, 0x00, 0x18, 0xFE});                       // EI; NOP; JR $
    load(0x66, {0x3C, 0xED, 0x45});                           // INC A; RETN
    cpu.run(8);
    cpu.trigger_nmi();
    t = cpu.run(1);
    CHECK(t == 11 && cpu.pc == 0x66 && !cpu.iff1 && cpu.iff2 && !cpu.nmi_pending, "NMI accept t=%d", t);
    cpu.run(1);
    t = cpu.run(1);
    CHECK(t == 14 && cpu.iff1 && cpu.pc == 2, "RETN");

    // NMI wakes HALT and has priority over a pending IRQ.
    fresh(0, {0x76});
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = true;
    cpu.run(1);
    cpu.set_irq(true);
    cpu.trigger_nmi();
    t = cpu.run(1);
    CHECK(t == 11 && cpu.pc == 0x66 && !cpu.halted && mem[0xEFFE] == 0x01, "NMI from HALT");

    // IM 2: vector read from (I << 8) | FF.
    fresh(0, {0xED, 0x5E, 0x3E, 0x80, 0xED, 0x47, 0xFB, 0x00, 0x76});
    load(0x80FF, {0x34, 0x12});
    run_to_halt();
    cpu.set_irq(true);
    t = cpu.run(1);
    CHECK(t == 19 && cpu.pc == 0x1234 && mem[0xEFFE] == 0x09 && !cpu.iff1, "IM2 t=%d PC=%04X", t, cpu.pc);

    // IM 0 behaves as RST 38h.
    fresh(0, {0xFB, 0x00, 0x00});
    cpu.set_irq(true);
    cpu.run(1); cpu.run(1);
    t = cpu.run(1);
    CHECK(t == 13 && cpu.pc == 0x38 && cpu.im == 0, "IM0 t=%d", t);

    // A 60 Hz frame loop in the style of a sound driver: /INT held for 40 cycles per frame.
    fresh(0, {
        0x31, 0x00, 0x20,       // LD SP,2000
        0xED, 0x56,             // IM 1
        0xFB,                   // EI
        0x3A, 0x00, 0x10,       // loop: LD A,(1000)
        0x3C,                   // INC A
        0x32, 0x00, 0x10,       // LD (1000),A
        0x18, 0xF7,             // JR loop
    });
    load(0x38, {0xF5, 0x3A, 0x01, 0x10, 0x3C, 0x32, 0x01, 0x10, 0xF1, 0xFB, 0xED, 0x4D});
    long long total = 0;
    for (int f = 0; f < 60; f++) {
        cpu.set_irq(true);
        int ft = cpu.run(40);
        cpu.set_irq(false);
        ft += cpu.run(59736 - ft);
        total += ft;
    }
    CHECK(mem[0x1001] == 60 && total >= 60 * 59736 && total < 60 * 59736 + 60 * 20,
          "frame loop: %d ISRs, %lld T", mem[0x1001], total);
}

// ---------------------------------------------------------------------------
// T-state tables (also checks instruction lengths via PC and the R counter).

const uint8_t T_MAIN[256] = {
//   0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
     4, 10,  7,  6,  4,  4,  7,  4,  4, 11,  7,  6,  4,  4,  7,  4,   // 0
    13, 10,  7,  6,  4,  4,  7,  4, 12, 11,  7,  6,  4,  4,  7,  4,   // 1
    12, 10, 16,  6,  4,  4,  7,  4, 12, 11, 16,  6,  4,  4,  7,  4,   // 2
    12, 10, 13,  6, 11, 11, 10,  4, 12, 11, 13,  6,  4,  4,  7,  4,   // 3
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // 4
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // 5
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // 6
     7,  7,  7,  7,  7,  7,  4,  7,  4,  4,  4,  4,  4,  4,  7,  4,   // 7
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // 8
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // 9
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // A
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,   // B
    11, 10, 10, 10, 17, 11,  7, 11, 11, 10, 10,  0, 17, 17,  7, 11,   // C
    11, 10, 10, 11, 17, 11,  7, 11, 11,  4, 10, 11, 17,  0,  7, 11,   // D
    11, 10, 10, 19, 17, 11,  7, 11, 11,  4, 10,  4, 17,  0,  7, 11,   // E
    11, 10, 10,  4, 17, 11,  7, 11, 11,  6, 10,  4, 17,  0,  7, 11,   // F
};

const uint8_t T_ED[64] = {   // ED 40-7F
    12, 12, 15, 20,  8, 14,  8,  9, 12, 12, 15, 20,  8, 14,  8,  9,
    12, 12, 15, 20,  8, 14,  8,  9, 12, 12, 15, 20,  8, 14,  8,  9,
    12, 12, 15, 20,  8, 14,  8, 18, 12, 12, 15, 20,  8, 14,  8, 18,
    12, 12, 15, 20,  8, 14,  8,  8, 12, 12, 15, 20,  8, 14,  8,  8,
};

bool is_cond_flow(int op) { return (op & 0xC7) == 0xC0 || (op & 0xC7) == 0xC2 || (op & 0xC7) == 0xC4; }

int main_time(int op, uint8_t f, uint8_t b) {
    if (op == 0x10) return b != 1 ? 13 : 8;
    if ((op & 0xE7) == 0x20) return cc_true((op >> 3) & 3, f) ? 12 : 7;
    if ((op & 0xC7) == 0xC0) return cc_true((op >> 3) & 7, f) ? 11 : 5;
    if ((op & 0xC7) == 0xC4) return cc_true((op >> 3) & 7, f) ? 17 : 10;
    return T_MAIN[op];
}

int main_len(int op) {
    if ((op & 0xC7) == 0x06 || (op & 0xC7) == 0xC6 || op == 0xD3 || op == 0xDB) return 2;
    if ((op & 0xE7) == 0x20 || op == 0x10 || op == 0x18) return 2;
    if ((op & 0xCF) == 0x01 || op == 0x22 || op == 0x2A || op == 0x32 || op == 0x3A) return 3;
    if ((op & 0xC7) == 0xC2 || op == 0xC3 || (op & 0xC7) == 0xC4 || op == 0xCD) return 3;
    return 1;
}

// Operands and stack are zero, so every taken jump/call/return lands on 0000.
uint16_t main_next_pc(int op, uint16_t org, uint8_t f, uint16_t hl) {
    if (op == 0xC3 || op == 0xCD || op == 0xC9) return 0;
    if (op == 0xE9) return hl;
    if ((op & 0xC7) == 0xC7) return uint16_t(op & 0x38);
    if (is_cond_flow(op) && cc_true((op >> 3) & 7, f)) return 0;
    return uint16_t(org + main_len(op));
}

bool uses_ixd(int op) {
    if (op == 0x34 || op == 0x35 || op == 0x36) return true;
    if (op < 0x40 || op >= 0xC0 || op == 0x76) return false;
    return (op & 7) == 6 || (op >= 0x70 && op < 0x78);
}

int index_time(int op, uint8_t f, uint8_t b) {
    switch (op) {
    case 0x21: case 0xE1: return 14;
    case 0x22: case 0x2A: return 20;
    case 0x23: case 0x2B: case 0xF9: return 10;
    case 0x09: case 0x19: case 0x29: case 0x39: case 0xE5: return 15;
    case 0x34: case 0x35: case 0xE3: return 23;
    case 0x36: return 19;
    case 0xE9: return 8;
    }
    return uses_ixd(op) ? 19 : 4 + main_time(op, f, b);
}

void test_timing() {
    for (int op = 0; op < 256; op++) {
        if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) continue;
        for (int pass = 0; pass < 2; pass++) {
            const uint8_t f = pass ? 0xFF : 0x00, b = pass ? 1 : 2;
            fresh(0x1000, {op, 0, 0});
            cpu.af = f;
            cpu.bc = uint16_t(b << 8);
            const int t = cpu.run(1);
            const int want = main_time(op, f, b);
            const uint16_t want_pc = main_next_pc(op, 0x1000, f, 0);
            CHECK(t == want && cpu.pc == want_pc && cpu.r == 1, "op %02X F=%02X: t=%d want %d, PC=%04X want %04X",
                  op, f, t, want, cpu.pc, want_pc);
        }
    }
    for (int op = 0; op < 256; op++) {
        fresh(0x1000, {0xCB, op});
        cpu.hl = 0x2000;
        const int t = cpu.run(1);
        const int want = (op & 7) != 6 ? 8 : (op & 0xC0) == 0x40 ? 12 : 15;
        CHECK(t == want && cpu.pc == 0x1002 && cpu.r == 2, "CB %02X: t=%d want %d", op, t, want);
    }
    for (int op = 0; op < 256; op++)
        for (int pass = 0; pass < 2; pass++) {
            const bool block = (op & 0xE4) == 0xA0, repeat = block && (op & 0x10) && pass == 0;
            fresh(0x1000, {0xED, op, 0, 0});
            cpu.af = 0x5500;
            cpu.hl = 0x2000;
            cpu.de = 0x3000;
            cpu.bc = pass == 0 ? 0x0202 : (op & 3) >= 2 ? 0x0101 : 0x0001;
            const int t = cpu.run(1);
            int want = 8;
            if (op >= 0x40 && op < 0x80) want = T_ED[op - 0x40];
            if (block) want = repeat ? 21 : 16;
            uint16_t want_pc = 0x1002;
            if ((op & 0xC7) == 0x43) want_pc = 0x1004;       // LD (nn),rr / LD rr,(nn)
            if ((op & 0xC7) == 0x45) want_pc = 0x0000;       // RETN/RETI pop 0000
            if (repeat) want_pc = 0x1000;
            const int want_r = op == 0x4F ? 0x55 : 2;         // LD R,A
            CHECK(t == want && cpu.pc == want_pc && cpu.r == want_r, "ED %02X pass %d: t=%d want %d, PC=%04X want %04X",
                  op, pass, t, want, cpu.pc, want_pc);
        }
    for (int pfx : {0xDD, 0xFD}) {
        for (int op = 0; op < 256; op++) {
            if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) continue;
            fresh(0x1000, {pfx, op, 0, 0, 0});
            cpu.ix = cpu.iy = 0x3000;
            cpu.bc = 0x0200;
            cpu.af = 0x0000;
            const int t = cpu.run(1);
            const int want = index_time(op, 0x00, 2);
            uint16_t want_pc = main_next_pc(op, 0x1001, 0x00, 0x3000);
            if (uses_ixd(op)) want_pc = uint16_t(want_pc + 1);
            CHECK(t == want && cpu.pc == want_pc && cpu.r == 2, "%02X %02X: t=%d want %d, PC=%04X want %04X",
                  pfx, op, t, want, cpu.pc, want_pc);
        }
        for (int op = 0; op < 256; op++) {
            fresh(0x1000, {pfx, 0xCB, 0x05, op});
            cpu.ix = cpu.iy = 0x3000;
            const int t = cpu.run(1);
            const int want = (op & 0xC0) == 0x40 ? 20 : 23;
            CHECK(t == want && cpu.pc == 0x1004 && cpu.r == 2, "%02X CB d %02X: t=%d want %d", pfx, op, t, want);
        }
    }
}

// ---------------------------------------------------------------------------
// Miscellaneous behaviour

void test_misc() {
    // R: 7-bit counter, bit 7 preserved.
    fresh(0, {0x00});
    cpu.r = 0x7F;
    cpu.run(1);
    CHECK(cpu.r == 0x00, "R wrap 7F");
    fresh(0, {0x00});
    cpu.r = 0xFF;
    cpu.run(1);
    CHECK(cpu.r == 0x80, "R wrap FF");

    // LD R,A then LD A,R (R has advanced for NOPs and both ED 5F fetches).
    fresh(0, {0x3E, 0x80, 0xED, 0x4F, 0x00, 0x00, 0x00, 0xED, 0x5F, 0x76});
    run_to_halt();
    CHECK(A() == 0x85 && (F() & (S | Z | V | N | H)) == S, "LD A,R A=%02X F=%02X", A(), F());

    // LD A,I: P/V = IFF2.
    fresh(0, {0xFB, 0x3E, 0x00, 0xED, 0x47, 0xED, 0x57, 0x76});
    int t = run_to_halt();
    CHECK(t == 4 + 7 + 9 + 9 + 4 && A() == 0 && (F() & (Z | V)) == (Z | V), "LD A,I with IFF2=1 F=%02X", F());
    fresh(0, {0xF3, 0x3E, 0x9C, 0xED, 0x47, 0xED, 0x57, 0x76});
    run_to_halt();
    CHECK(A() == 0x9C && (F() & (S | Z | V)) == S, "LD A,I with IFF2=0 F=%02X", F());

    // MEMPTR leaks into BIT n,(HL): LD A,(2800) leaves WZ = 2801.
    fresh(0, {0x3A, 0x00, 0x28, 0x21, 0x00, 0x40, 0xCB, 0x46, 0x76});
    mem[0x4000] = 0x01;
    run_to_halt();
    CHECK(F() == (Y | H | X | C), "BIT 0,(HL) after LD A,(2800) F=%02X", F());

    // Port I/O.
    fresh(0, {
        0x01, 0x34, 0x12,       // LD BC,1234       10
        0xED, 0x78,             // IN A,(C)         12
        0xED, 0x71,             // OUT (C),0        12
        0x3E, 0x56,             // LD A,56          7
        0xD3, 0x78,             // OUT (78),A       11
        0xDB, 0x9A,             // IN A,(9A)        11
        0xED, 0x70,             // IN F,(C)         12
        0x76,
    });
    in_data[0] = 0x80; in_data[1] = 0x00; in_data[2] = 0x00;
    t = run_to_halt();
    CHECK(t == 79, "I/O T=%d", t);
    CHECK(in_log.size() == 3 && in_log[0] == 0x1234 && in_log[1] == 0x569A && in_log[2] == 0x1234, "IN ports");
    CHECK(out_log.size() == 2 && out_log[0] == std::make_pair(uint16_t(0x1234), uint8_t(0)) &&
          out_log[1] == std::make_pair(uint16_t(0x5678), uint8_t(0x56)), "OUT ports");
    CHECK(A() == 0 && F() == (Z | V | C), "IN F,(C) F=%02X", F());
    fresh(0, {0xED, 0x78});                                     // IN r,(C) sets S/Z/P, keeps C
    cpu.in = nullptr;
    cpu.af = 0x0000;
    cpu.run(1);
    CHECK(A() == 0xFF && F() == (S | Y | X | V), "IN with no handler F=%02X", F());

    // RRD / RLD.
    fresh(0, {0x21, 0x00, 0x40, 0x3E, 0x12, 0xED, 0x67, 0x76});
    mem[0x4000] = 0x34;
    t = run_to_halt();
    CHECK(t == 10 + 7 + 18 + 4 && A() == 0x14 && mem[0x4000] == 0x23 && cpu.wz == 0x4001, "RRD A=%02X m=%02X", A(), mem[0x4000]);
    fresh(0, {0x21, 0x00, 0x40, 0x3E, 0x12, 0xED, 0x6F, 0x76});
    mem[0x4000] = 0x34;
    run_to_halt();
    CHECK(A() == 0x13 && mem[0x4000] == 0x42 && F() == (C), "RLD A=%02X m=%02X F=%02X", A(), mem[0x4000], F());

    // LD (nn),rr / LD rr,(nn) via ED and IX.
    fresh(0, {0xED, 0x43, 0x00, 0x40, 0xED, 0x5B, 0x00, 0x40, 0xDD, 0x22, 0x02, 0x40, 0xFD, 0x2A, 0x02, 0x40, 0x76});
    cpu.bc = 0xBEEF; cpu.ix = 0xCAFE;
    run_to_halt();
    CHECK(cpu.de == 0xBEEF && cpu.iy == 0xCAFE && mem[0x4000] == 0xEF && mem[0x4003] == 0xCA, "LD (nn),rr");

    // IM modes (including the undocumented mirrors).
    fresh(0, {0xED, 0x5E, 0xED, 0x6E, 0xED, 0x76, 0x76});
    cpu.run(8);  CHECK(cpu.im == 2, "IM 2");
    cpu.run(8);  CHECK(cpu.im == 0, "ED 6E -> IM 0");
    cpu.run(8);  CHECK(cpu.im == 1, "ED 76 -> IM 1");

    // run(): budget semantics and wait states.
    fresh(0, {});
    CHECK(cpu.run(0) == 0 && cpu.run(-3) == 0 && cpu.pc == 0, "run(<=0)");
    CHECK(cpu.run(5) == 8 && cpu.pc == 2, "run overshoot");
    fresh(0, {0x3A, 0x00, 0x80, 0x00});
    high_wait = 3;
    CHECK(cpu.run(1) == 16 && cpu.wait_cycles == 0, "wait cycles added");
    CHECK(cpu.run(1) == 4, "no wait for low memory");
    fresh(0, {0x3A, 0x00, 0x80, 0x00});
    high_wait = 3;
    CHECK(cpu.run(14) == 16, "waits count toward the budget");

    // reset()
    cpu.i = 5; cpu.r = 9; cpu.im = 2; cpu.iff1 = cpu.iff2 = true; cpu.halted = true; cpu.ei_delay = 1;
    cpu.pc = 0x1234; cpu.af = 0; cpu.sp = 0;
    cpu.reset();
    CHECK(cpu.pc == 0 && cpu.i == 0 && cpu.r == 0 && cpu.im == 0 && !cpu.iff1 && !cpu.iff2 && !cpu.halted &&
          cpu.af == 0xFFFF && cpu.sp == 0xFFFF && cpu.ei_delay == 0, "reset state");
}

void test_run_control() {
    // end_run() returns after the requesting instruction; executed() timestamps writes.
    fresh(0, {
        0x3E, 0x01,             // LD A,1           7
        0x32, 0x00, 0x7F,       // LD (7F00),A      13  stamp 7
        0x00,                   // NOP              4
        0x32, 0x00, 0x7F,       // LD (7F00),A      13  stamp 24
        0x32, 0x01, 0x7F,       // LD (7F01),A      13  end_run
        0x00, 0x00,
    });
    CHECK(cpu.executed() == 0, "executed() before run");
    int t = cpu.run(1000);
    CHECK(t == 50 && cpu.pc == 0x000C && cpu.executed() == 50, "end_run t=%d PC=%04X", t, cpu.pc);
    CHECK(stamps.size() == 2 && stamps[0] == 7 && stamps[1] == 24, "executed() stamps");
    t = cpu.run(8);
    CHECK(t == 8 && cpu.executed() == 8, "run after end_run");

    // Wait states from earlier in the same instruction are included.
    fresh(0, {0x21, 0x00, 0x80, 0x11, 0x00, 0x7F, 0x01, 0x01, 0x00, 0xED, 0xA0, 0x76});
    high_wait = 3;
    t = cpu.run(1000);
    CHECK(stamps.size() == 1 && stamps[0] == 33, "executed() with waits: %d", stamps.empty() ? -1 : stamps[0]);
    CHECK(t >= 1000 && t < 1004 && cpu.executed() == t && cpu.halted, "halt fast-forward t=%d", t);
    cpu.run(0);
    CHECK(cpu.executed() == 0, "executed() after run(0)");
}

bool same_regs(const Z80& a, const Z80& b) {
    return a.af == b.af && a.bc == b.bc && a.de == b.de && a.hl == b.hl && a.af2 == b.af2 && a.bc2 == b.bc2 &&
           a.de2 == b.de2 && a.hl2 == b.hl2 && a.ix == b.ix && a.iy == b.iy && a.sp == b.sp && a.pc == b.pc &&
           a.wz == b.wz && a.i == b.i && a.r == b.r && a.im == b.im && a.iff1 == b.iff1 && a.iff2 == b.iff2 &&
           a.halted == b.halted && a.irq_line == b.irq_line && a.nmi_pending == b.nmi_pending &&
           a.ei_delay == b.ei_delay && a.wait_cycles == b.wait_cycles;
}

void test_state() {
    fresh(0, {0x0E, 0x05, 0x06, 0x04, 0x10, 0xFE, 0x0D, 0x20, 0xF9, 0x76});
    cpu.run(100);
    std::vector<uint8_t> st(cpu.state_size()), st2(cpu.state_size());
    cpu.save_state(st.data());
    cpu.save_state(st2.data());
    CHECK(cpu.state_size() > 0 && st == st2, "deterministic save");
    std::vector<uint8_t> ram(mem, mem + sizeof mem);
    const int t1 = run_to_halt();
    const Z80 after = cpu;

    cpu.reset();
    cpu.bc = 0xDEAD; cpu.ix = 0xBEEF; cpu.im = 2; cpu.iff1 = true;
    std::memcpy(mem, ram.data(), sizeof mem);
    cpu.load_state(st.data());
    const int t2 = run_to_halt();
    CHECK(t1 == t2 && same_regs(cpu, after), "state round trip");

    Z80 a = cpu;
    a.im = 2; a.iff1 = true; a.iff2 = false; a.ei_delay = 1; a.halted = true; a.nmi_pending = true;
    a.irq_line = true; a.i = 0x3C; a.r = 0xA5; a.wz = 0x7777; a.af2 = 0x1111; a.iy = 0x2222;
    std::vector<uint8_t> s3(a.state_size());
    a.save_state(s3.data());
    Z80 b;
    b.load_state(s3.data());
    CHECK(same_regs(a, b), "state fields");
}

void test_speed() {
    fresh(0, {
        0x21, 0x00, 0x40,       // LD HL,4000
        0x06, 0x00,             // LD B,0
        0x7E,                   // loop: LD A,(HL)
        0x80,                   // ADD A,B
        0x77,                   // LD (HL),A
        0x23,                   // INC HL
        0xDD, 0x86, 0x00,       // ADD A,(IX+0)
        0xCB, 0x3F,             // SRL A
        0x10, 0xF5,             // DJNZ loop
        0xC3, 0x00, 0x00,       // JP 0
    });
    cpu.ix = 0x5000;
    const auto t0 = std::chrono::steady_clock::now();
    long long total = 0;
    while (total < 300000000LL) total += cpu.run(228);        // one scanline per slice
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double mhz = total / sec / 1e6;
    std::printf("    speed: %.0f MHz effective (%.2f s for %lld T-states)\n", mhz, sec, total);
    CHECK(mhz > 50.0, "too slow: %.1f MHz", mhz);
}

} // namespace

int main() {
    section("8-bit ALU (exhaustive)", test_alu8);
    section("INC/DEC (exhaustive)", test_incdec);
    section("DAA/CPL/SCF/CCF/NEG/rotate A (exhaustive)", test_acc_ops);
    section("CB rotates/BIT/SET/RES", test_cb_ops);
    section("16-bit ADD/ADC/SBC", test_arith16);
    section("block instructions", test_block);
    section("stack/call/ret", test_stack);
    section("IX/IY indexing", test_index);
    section("DJNZ/JR timing", test_djnz);
    section("interrupts", test_interrupts);
    section("T-state tables", test_timing);
    section("misc", test_misc);
    section("run control", test_run_control);
    section("save states", test_state);
    section("speed", test_speed);
    std::printf("%s: %lld checks, %d failures\n", total_fails ? "FAIL" : "PASS", total_checks, total_fails);
    return total_fails ? 1 : 0;
}
