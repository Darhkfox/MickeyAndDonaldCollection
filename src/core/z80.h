// Zilog Z80 interpreter used as the Mega Drive sound co-processor.
#pragma once
#include <cstdint>
#include <cstddef>

class Z80 {
public:
    typedef uint8_t (*ReadFn)(void* ctx, uint16_t addr);
    typedef void    (*WriteFn)(void* ctx, uint16_t addr, uint8_t v);
    typedef uint8_t (*InFn)(void* ctx, uint16_t port);
    typedef void    (*OutFn)(void* ctx, uint16_t port, uint8_t v);

    // Bus callbacks (must be set before reset/run).
    void*   ctx   = nullptr;
    ReadFn  read  = nullptr;
    WriteFn write = nullptr;
    InFn    in    = nullptr;   // may be null: reads return 0xFF
    OutFn   out   = nullptr;   // may be null: writes ignored

    // Register file (public for debugging / save states).
    uint16_t af = 0xFFFF, bc = 0, de = 0, hl = 0;
    uint16_t af2 = 0, bc2 = 0, de2 = 0, hl2 = 0;
    uint16_t ix = 0, iy = 0, sp = 0xFFFF, pc = 0;
    uint16_t wz = 0;           // internal MEMPTR
    uint8_t  i = 0, r = 0;     // r: low 7 bits counted, bit 7 preserved
    uint8_t  im = 0;           // interrupt mode 0/1/2
    bool     iff1 = false, iff2 = false;
    bool     halted = false;
    bool     irq_line = false; // level-triggered /INT
    bool     nmi_pending = false;
    int      ei_delay = 0;     // interrupts are not accepted right after EI

    // Cycle accounting. `wait_cycles` may be incremented by bus callbacks
    // (e.g. when the Z80 accesses the 68000 bus) and is added to the count.
    int      wait_cycles = 0;

    void reset();
    // Execute until at least `cycles` T-states have elapsed. Returns T-states consumed.
    int  run(int cycles);
    void set_irq(bool asserted) { irq_line = asserted; }
    void trigger_nmi() { nmi_pending = true; }

    // T-states consumed so far by the current run(): completed instructions plus
    // wait_cycles accumulated so far (the current instruction's own cycles are
    // not included). Outside run() this is the total of the last run.
    int  executed() const { return run_active ? run_done + wait_cycles : run_done; }
    // Make run() return as soon as the current instruction completes.
    void end_run() { run_stop = true; }

    // Save-state support: plain register block.
    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

private:
    int  run_done = 0;         // T-states consumed by the current/last run()
    bool run_active = false;
    bool run_stop = false;

    int  step();               // execute one instruction (incl. prefixes), return T-states
    int  take_interrupt();

    // Decoder internals (z80.cpp). `xy` is HL, or IX/IY under a DD/FD prefix.
    int       exec_main(uint8_t op, uint16_t& xy);
    int       exec_cb();
    int       exec_ed();
    int       exec_xycb(uint16_t xy);
    int       block(uint8_t op);
    uint8_t   fetch_op();      // M1 cycle: also advances R
    uint8_t   fetch8();
    uint16_t  fetch16();
    uint16_t  rd16(uint16_t a);
    void      wr16(uint16_t a, uint16_t v);
    void      push(uint16_t v);
    uint16_t  pop();
    uint8_t   port_in(uint16_t port);
    void      port_out(uint16_t port, uint8_t v);
    uint8_t&  r8(int n, uint16_t& xy);
    uint16_t& rp(int n, uint16_t& xy);
    bool      cond(int cc) const;
    void      alu(int op, uint8_t v);
    uint8_t   inc8(uint8_t v);
    uint8_t   dec8(uint8_t v);
    uint8_t   rot(int op, uint8_t v);
    void      bit(int n, uint8_t v, uint8_t xy_src);
    uint16_t  add16(uint16_t a, uint16_t b);
    void      adc16(uint16_t v, bool sub);
    void      daa();
};
