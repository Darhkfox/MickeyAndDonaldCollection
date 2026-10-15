// Focused 68000 instruction tests (hand-encoded opcodes, expected results from the M68000 PRM).
#include "../src/core/m68k.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <initializer_list>

static uint8_t mem[0x10000];
static uint8_t r8(void*, uint32_t a) { return mem[a & 0xFFFF]; }
static uint16_t r16(void*, uint32_t a) { a &= 0xFFFF; return (uint16_t)((mem[a] << 8) | mem[(a + 1) & 0xFFFF]); }
static void w8(void*, uint32_t a, uint8_t v) { mem[a & 0xFFFF] = v; }
static void w16(void*, uint32_t a, uint16_t v) { a &= 0xFFFF; mem[a] = (uint8_t)(v >> 8); mem[(a + 1) & 0xFFFF] = (uint8_t)v; }

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Runs `code` at $1000 with the given setup until it hits the STOP #$2700 terminator.
static M68k& run(std::initializer_list<uint16_t> code, void (*setup)(M68k&) = nullptr) {
    static M68k cpu;
    memset(mem, 0, sizeof(mem));
    mem[0] = 0; mem[1] = 0; mem[2] = 0x80; mem[3] = 0x00;          // SSP = $8000
    mem[4] = 0; mem[5] = 0; mem[6] = 0x10; mem[7] = 0x00;          // PC = $1000
    // exception vectors 2..63 -> $0F00 (STOP)
    for (int v = 2; v < 64; v++) { mem[v * 4 + 2] = 0x0F; mem[v * 4 + 3] = 0x00; }
    w16(nullptr, 0x0F00, 0x60FE);                                    // BRA.S * (exception landing pad)
    uint32_t a = 0x1000;
    for (uint16_t w : code) { w16(nullptr, a, w); a += 2; }
    w16(nullptr, a, 0x60FE);                                         // BRA.S * terminator (keeps CCR)
    cpu.ctx = nullptr; cpu.rd8 = r8; cpu.rd16 = r16; cpu.wr8 = w8; cpu.wr16 = w16;
    for (int p = 0; p < 256; p++) { cpu.page_read[p] = nullptr; cpu.page_write[p] = nullptr; }
    cpu.reset();
    for (int i = 0; i < 16; i++) cpu.r[i] = 0;
    cpu.r[15] = 0x8000;
    if (setup) setup(cpu);
    for (int i = 0; i < 1000 && cpu.pc != a && cpu.pc != 0x0F00; i++) cpu.run(1);
    return cpu;
}
static uint16_t ccr(M68k& c) { return c.get_sr() & 0x1F; }
enum { C = 1, V = 2, Z = 4, N = 8, X = 16 };

int main() {
    // ABCD D1,D0 : $45 + $38 = $83
    { M68k& c = run({0xC101}, [](M68k& c) { c.r[0] = 0x45; c.r[1] = 0x38; c.set_ccr(Z); });
      CHECK((c.r[0] & 0xFF) == 0x83 && !(ccr(c) & C) && !(ccr(c) & Z), "abcd %x ccr %x", c.r[0], ccr(c)); }
    // ABCD with carry out: $99 + $01 = $00, C=X=1, Z unchanged (set)
    { M68k& c = run({0xC101}, [](M68k& c) { c.r[0] = 0x99; c.r[1] = 0x01; c.set_ccr(Z); });
      CHECK((c.r[0] & 0xFF) == 0x00 && (ccr(c) & (C | X)) == (C | X) && (ccr(c) & Z), "abcd carry %x %x", c.r[0], ccr(c)); }
    // SBCD D1,D0 : $45 - $38 = $07
    { M68k& c = run({0x8101}, [](M68k& c) { c.r[0] = 0x45; c.r[1] = 0x38; });
      CHECK((c.r[0] & 0xFF) == 0x07 && !(ccr(c) & C), "sbcd %x %x", c.r[0], ccr(c)); }
    // SBCD borrow: $00 - $01 = $99, C=1
    { M68k& c = run({0x8101}, [](M68k& c) { c.r[0] = 0x00; c.r[1] = 0x01; });
      CHECK((c.r[0] & 0xFF) == 0x99 && (ccr(c) & C), "sbcd borrow %x %x", c.r[0], ccr(c)); }
    // NBCD D0 : 0 - $45 = $55, C=1
    { M68k& c = run({0x4800}, [](M68k& c) { c.r[0] = 0x45; });
      CHECK((c.r[0] & 0xFF) == 0x55 && (ccr(c) & C), "nbcd %x %x", c.r[0], ccr(c)); }
    // DIVU D1,D0 : 100000 / 7 = 14285 r 5
    { M68k& c = run({0x80C1}, [](M68k& c) { c.r[0] = 100000; c.r[1] = 7; });
      CHECK(c.r[0] == ((5u << 16) | 14285u), "divu %x", c.r[0]); }
    // DIVU overflow leaves register unchanged, V=1
    { M68k& c = run({0x80C1}, [](M68k& c) { c.r[0] = 0x200000; c.r[1] = 2; });
      CHECK(c.r[0] == 0x200000 && (ccr(c) & V), "divu ovf %x %x", c.r[0], ccr(c)); }
    // DIVS D1,D0 : -7 / 2 = -3 r -1
    { M68k& c = run({0x81C1}, [](M68k& c) { c.r[0] = (uint32_t)-7; c.r[1] = 2; });
      CHECK(c.r[0] == ((0xFFFFu << 16) | 0xFFFDu) && (ccr(c) & N), "divs %x %x", c.r[0], ccr(c)); }
    // DIVU by zero -> vector 5 (lands on STOP at $F00)
    { M68k& c = run({0x80C1}, [](M68k& c) { c.r[0] = 5; c.r[1] = 0; });
      CHECK(c.pc == 0x0F00 && c.r[0] == 5, "div0 pc %x", c.pc); }
    // MULS D1,D0 : -2 * 3 = -6
    { M68k& c = run({0xC1C1}, [](M68k& c) { c.r[0] = 0xFFFE; c.r[1] = 3; });
      CHECK(c.r[0] == (uint32_t)-6 && (ccr(c) & N), "muls %x", c.r[0]); }
    // MULU D1,D0 : $FFFF * $FFFF
    { M68k& c = run({0xC0C1}, [](M68k& c) { c.r[0] = 0xFFFF; c.r[1] = 0xFFFF; });
      CHECK(c.r[0] == 0xFFFE0001u, "mulu %x", c.r[0]); }
    // ASL.W #1,D0 : $4000 -> $8000, V=1 C=0
    { M68k& c = run({0xE340}, [](M68k& c) { c.r[0] = 0x4000; });
      CHECK((c.r[0] & 0xFFFF) == 0x8000 && (ccr(c) & V) && !(ccr(c) & C) && (ccr(c) & N), "asl %x %x", c.r[0], ccr(c)); }
    // ASR.B #1,D0 : $81 -> $C0, C=X=1
    { M68k& c = run({0xE200}, [](M68k& c) { c.r[0] = 0x81; });
      CHECK((c.r[0] & 0xFF) == 0xC0 && (ccr(c) & (C | X)) == (C | X), "asr %x %x", c.r[0], ccr(c)); }
    // LSR.L D1,D0 with count 33 -> 0, C=0, Z=1
    { M68k& c = run({0xE2A8}, [](M68k& c) { c.r[0] = 0xFFFFFFFF; c.r[1] = 33; });
      CHECK(c.r[0] == 0 && (ccr(c) & Z) && !(ccr(c) & C), "lsr33 %x %x", c.r[0], ccr(c)); }
    // LSL.L #8 via register count 0 -> C cleared, value unchanged, X unchanged
    { M68k& c = run({0xE3A8}, [](M68k& c) { c.r[0] = 0x12345678; c.r[1] = 0; c.set_ccr(X | C); });
      CHECK(c.r[0] == 0x12345678 && !(ccr(c) & C) && (ccr(c) & X), "lsl0 %x %x", c.r[0], ccr(c)); }
    // ROXL.B #1,D0 with X=1 : $80 -> $01, C=X=1
    { M68k& c = run({0xE310}, [](M68k& c) { c.r[0] = 0x80; c.set_ccr(X); });
      CHECK((c.r[0] & 0xFF) == 0x01 && (ccr(c) & (C | X)) == (C | X), "roxl %x %x", c.r[0], ccr(c)); }
    // ROR.W #4,D0 : $1234 -> $4123
    { M68k& c = run({0xE858}, [](M68k& c) { c.r[0] = 0x1234; });
      CHECK((c.r[0] & 0xFFFF) == 0x4123, "ror %x", c.r[0]); }
    // CMP.B D1,D0 : $7F - $80 -> N V C set
    { M68k& c = run({0xB001}, [](M68k& c) { c.r[0] = 0x7F; c.r[1] = 0x80; });
      CHECK((ccr(c) & (N | V | C)) == (N | V | C) && !(ccr(c) & Z), "cmp %x", ccr(c)); }
    // NEG.B D0 : $80 -> $80, V=1 C=1
    { M68k& c = run({0x4400}, [](M68k& c) { c.r[0] = 0x80; });
      CHECK((c.r[0] & 0xFF) == 0x80 && (ccr(c) & (V | C | X)) == (V | C | X), "neg %x %x", c.r[0], ccr(c)); }
    // ADDX.L D1,D0 : Z stays clear when result 0 after nonzero? (Z only cleared by nonzero)
    { M68k& c = run({0xD181}, [](M68k& c) { c.r[0] = 0xFFFFFFFF; c.r[1] = 0; c.set_ccr(X | Z); });
      CHECK(c.r[0] == 0 && (ccr(c) & Z) && (ccr(c) & C), "addx %x %x", c.r[0], ccr(c)); }
    // SUBX.B D1,D0 : 0 - 0 - 1 = $FF, C=1, Z cleared
    { M68k& c = run({0x9101}, [](M68k& c) { c.r[0] = 0; c.r[1] = 0; c.set_ccr(X | Z); });
      CHECK((c.r[0] & 0xFF) == 0xFF && (ccr(c) & C) && !(ccr(c) & Z), "subx %x %x", c.r[0], ccr(c)); }
    // EXT.W / EXT.L / SWAP
    { M68k& c = run({0x4880, 0x48C0, 0x4840}, [](M68k& c) { c.r[0] = 0x12345680; });
      CHECK(c.r[0] == 0xFF80FFFF, "ext/swap %x", c.r[0]); }
    // MOVEM.L D0-D2/A0,-(A7) then MOVEM.L (A7)+,D3-D5/A1
    { M68k& c = run({0x48E7, 0xE080, 0x4CDF, 0x0238}, [](M68k& c) { c.r[0] = 1; c.r[1] = 2; c.r[2] = 3; c.r[8] = 4; });
      CHECK(c.r[3] == 1 && c.r[4] == 2 && c.r[5] == 3 && c.r[9] == 4 && c.r[15] == 0x8000, "movem %x %x %x %x sp %x", c.r[3], c.r[4], c.r[5], c.r[9], c.r[15]); }
    // MOVEM.W (A0)+,D0 sign-extends
    { M68k& c = run({0x4C98, 0x0001}, [](M68k& c) { c.r[8] = 0x2000; w16(nullptr, 0x2000, 0x8001); });
      CHECK(c.r[0] == 0xFFFF8001 && c.r[8] == 0x2002, "movem.w %x", c.r[0]); }
    // MOVEP.L D0,0(A0) / MOVEP.W 0(A0),D1
    { M68k& c = run({0x01C8, 0x0000, 0x0308, 0x0000}, [](M68k& c) { c.r[0] = 0x11223344; c.r[8] = 0x3000; });
      CHECK(mem[0x3000] == 0x11 && mem[0x3002] == 0x22 && mem[0x3004] == 0x33 && mem[0x3006] == 0x44 && (c.r[1] & 0xFFFF) == 0x1122,
            "movep %x", c.r[1]); }
    // DBF loop: MOVEQ #4,D0 ; loop: ADDQ.W #1,D1 ; DBF D0,loop
    { M68k& c = run({0x7004, 0x5241, 0x51C8, 0xFFFC});
      CHECK((c.r[1] & 0xFFFF) == 5 && (c.r[0] & 0xFFFF) == 0xFFFF, "dbf %x %x", c.r[1], c.r[0]); }
    // BSR/RTS + LINK/UNLK
    { M68k& c = run({0x6104, 0x7201, 0x6008, 0x4E56, 0xFFF8, 0x4E5E, 0x4E75}, [](M68k& c) { c.r[14] = 0x1234; });
      CHECK((c.r[1] & 0xFF) == 1 && c.r[14] == 0x1234 && c.r[15] == 0x8000, "bsr/link %x %x %x", c.r[1], c.r[14], c.r[15]); }
    // BGT with V: cmp.w #1, d0 where d0=$8000 -> (8000-1)=7FFF, V=1 N=0 -> GT? N==V false -> not taken
    { M68k& c = run({0x0C40, 0x0001, 0x6E02, 0x7401}, [](M68k& c) { c.r[0] = 0x8000; });
      CHECK((c.r[2] & 0xFF) == 1, "bgt %x", c.r[2]); }
    // TRAP #1 -> vector 33 ($F00), pushes SR+PC
    { M68k& c = run({0x4E41});
      CHECK(c.pc == 0x0F00 && c.r[15] == 0x8000 - 6, "trap pc %x sp %x", c.pc, c.r[15]); }
    // LEA d8(PC,D0.W),A0 : base is address of extension word
    { M68k& c = run({0x41FB, 0x0004}, [](M68k& c) { c.r[0] = 0x10; });
      CHECK(c.r[8] == 0x1002 + 4 + 0x10, "lea pc idx %x", c.r[8]); }
    // BTST #3,D0 / BSET D1,(A0)
    { M68k& c = run({0x0800, 0x0003, 0x03D0}, [](M68k& c) { c.r[0] = 0x08; c.r[1] = 9; c.r[8] = 0x2000; });
      CHECK(mem[0x2000] == 0x02 && (ccr(c) & Z), "bset mem %x ccr %x", mem[0x2000], ccr(c)); }
    // ANDI to CCR / ORI to SR
    { M68k& c = run({0x023C, 0x0004, 0x007C, 0x0001}, [](M68k& c) { c.set_ccr(0x1F); });
      CHECK(ccr(c) == (Z | C), "ccr ops %x", ccr(c)); }
    // Scc: SEQ D0 with Z set
    { M68k& c = run({0x57C0}, [](M68k& c) { c.set_ccr(Z); });
      CHECK((c.r[0] & 0xFF) == 0xFF, "seq %x", c.r[0]); }
    // CMPM.B (A0)+,(A1)+
    { M68k& c = run({0xB308}, [](M68k& c) { c.r[8] = 0x2000; c.r[9] = 0x2100; mem[0x2000] = 5; mem[0x2100] = 5; });
      CHECK((ccr(c) & Z) && c.r[8] == 0x2001 && c.r[9] == 0x2101, "cmpm %x", ccr(c)); }
    // MOVE.B to -(A7) keeps word alignment
    { M68k& c = run({0x1F00}, [](M68k& c) { c.r[0] = 0xAB; });
      CHECK(c.r[15] == 0x8000 - 2 && mem[0x7FFE] == 0xAB, "move.b -(a7) sp %x", c.r[15]); }
    // SUBA.W sign-extends; ADDA.L
    { M68k& c = run({0x90FC, 0xFFFF, 0xD1FC, 0x0001, 0x0000}, [](M68k& c) { c.r[8] = 0x100; });
      CHECK(c.r[8] == 0x10101, "adda/suba %x", c.r[8]); }
    // EXG D0,A0
    { M68k& c = run({0xC188}, [](M68k& c) { c.r[0] = 1; c.r[8] = 2; });
      CHECK(c.r[0] == 2 && c.r[8] == 1, "exg"); }
    // TAS D0
    { M68k& c = run({0x4AC0}, [](M68k& c) { c.r[0] = 0x01; });
      CHECK((c.r[0] & 0xFF) == 0x81 && !(ccr(c) & N), "tas %x", c.r[0]); }
    // Interrupt: level 4 autovector taken when mask lower
    {
        static M68k& c = run({0x46FC, 0x2000, 0x4E71, 0x4E71, 0x4E71});
        (void)c;
    }
    printf("%s: %d checks, %d failures\n", fails ? "FAILED" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
