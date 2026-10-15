// Motorola 68000 interpreter (Mega Drive main CPU).
#pragma once
#include <cstdint>
#include <cstddef>

class M68k {
public:
    typedef uint8_t  (*Read8Fn)(void* ctx, uint32_t addr);
    typedef uint16_t (*Read16Fn)(void* ctx, uint32_t addr);
    typedef void     (*Write8Fn)(void* ctx, uint32_t addr, uint8_t v);
    typedef void     (*Write16Fn)(void* ctx, uint32_t addr, uint16_t v);
    // Interrupt acknowledge: return vector number, or -1 for autovector.
    typedef int      (*IrqAckFn)(void* ctx, int level);
    // PC hook: called before executing the instruction at a hooked address.
    typedef void     (*HookFn)(void* ctx, uint32_t pc);

    M68k();
    ~M68k();

    // ---- bus ----------------------------------------------------------------
    void*     ctx = nullptr;
    Read8Fn   rd8 = nullptr;
    Read16Fn  rd16 = nullptr;
    Write8Fn  wr8 = nullptr;
    Write16Fn wr16 = nullptr;
    IrqAckFn  irq_ack = nullptr;
    // Fast paths: 256 pages of 64 KB. Non-null pointer = direct big-endian byte
    // access (page base, offset = addr & 0xFFFF). Null = use the callbacks.
    uint8_t*  page_read[256];
    uint8_t*  page_write[256];

    // ---- state --------------------------------------------------------------
    uint32_t r[16];            // D0-D7, A0-A7 (A7 = active stack pointer)
    uint32_t usp = 0, ssp = 0; // inactive stack pointer storage
    uint32_t pc = 0;
    uint32_t ppc = 0;          // address of the instruction being executed
    uint16_t srh = 0x2700;     // T, S, I2-I0 bits of SR (CCR bits kept separately)
    uint32_t fx = 0, fn = 0, fz = 0, fv = 0, fc = 0;   // CCR flags as 0/1
    bool     stopped = false;
    int      irq_level = 0;    // currently asserted interrupt level (0 = none)

    // ---- execution ----------------------------------------------------------
    void reset();                    // loads SSP/PC from vectors 0/1
    int  run(int cycles);            // run at least `cycles`; returns cycles used
    int  executed() const { return cyc; }   // cycles consumed so far in current run()
    void end_run() { target = 0; }   // make run() return after the current instruction
    void add_cycles(int n) { cyc += n; }    // bus stalls (DMA)
    void set_irq(int level) { irq_level = level; }

    uint16_t get_sr() const;
    void     set_sr(uint16_t v);
    void     set_ccr(uint8_t v);

    // Hooks (for game-specific enhancements and debugging).
    void set_hook(uint32_t addr, bool on);
    void clear_hooks();
    HookFn hook_fn = nullptr;
    void*  hook_ctx = nullptr;
    // Called before every instruction when non-null (tracing / breakpoints).
    HookFn trace_fn = nullptr;
    void*  trace_ctx = nullptr;

    uint64_t total_cycles = 0;       // cycles since power on

    // Direct memory helpers usable by hooks (go through the bus).
    uint8_t  read8(uint32_t a);
    uint16_t read16(uint32_t a);
    uint32_t read32(uint32_t a);
    void     write8(uint32_t a, uint8_t v);
    void     write16(uint32_t a, uint16_t v);
    void     write32(uint32_t a, uint32_t v);
    void     push32(uint32_t v) { r[15] -= 4; write32(r[15], v); }
    uint32_t pop32() { uint32_t v = read32(r[15]); r[15] += 4; return v; }

    // Save states
    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

    // ---- internals used by the opcode handlers ------------------------------
    struct Ea { int kind; int reg; uint32_t addr; uint32_t imm; };
    enum { EA_D = 0, EA_A, EA_MEM, EA_IMM };

    int cyc = 0;
    int target = 0;
    uint8_t* hookmap = nullptr;     // 1 bit per word address (8M bits = 1 MB)
    bool     any_hooks = false;

    uint16_t fetch16();
    uint32_t fetch32();
    Ea       getea(int mode, int reg, int sz);
    uint32_t rd(const Ea& e, int sz);
    void     wr(const Ea& e, int sz, uint32_t v);
    uint32_t readsz(uint32_t a, int sz);
    void     writesz(uint32_t a, int sz, uint32_t v);
    uint32_t index_ea(uint32_t base);
    bool     cond(int c) const;
    void     exception(int vec, uint32_t ret_pc);
    void     take_irq(int level);
    void     set_nz(uint32_t v, int sz);
    void     push16(uint16_t v) { r[15] -= 2; write16(r[15], v); }
    uint16_t pop16() { uint16_t v = read16(r[15]); r[15] += 2; return v; }

    typedef void (*OpFn)(M68k& c, uint32_t op);
    static OpFn optable[65536];
    static void build_table();
};
