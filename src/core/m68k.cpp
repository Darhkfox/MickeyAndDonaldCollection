// Motorola 68000 interpreter.
// Table-driven decoder; cycle counts follow the MC68000 user manual closely
// enough for game timing (EA calculation times are added in getea()).
#include "m68k.h"
#include <cstring>
#include <utility>

M68k::OpFn M68k::optable[65536];

static inline uint32_t MASKS(int sz) { return sz == 1 ? 0xFFu : sz == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
static inline uint32_t MSBS(int sz) { return sz == 1 ? 0x80u : sz == 2 ? 0x8000u : 0x80000000u; }
static inline uint32_t sx8(uint32_t v) { return (uint32_t)(int32_t)(int8_t)(uint8_t)v; }
static inline uint32_t sx16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)(uint16_t)v; }
static inline uint32_t sxs(uint32_t v, int sz) { return sz == 1 ? sx8(v) : sz == 2 ? sx16(v) : v; }
static inline int size_std(int s) { return s == 0 ? 1 : s == 1 ? 2 : 4; }

M68k::M68k() {
    memset(r, 0, sizeof(r));
    memset(page_read, 0, sizeof(page_read));
    memset(page_write, 0, sizeof(page_write));
    hookmap = new uint8_t[1 << 20];
    memset(hookmap, 0, 1 << 20);
    build_table();
}

M68k::~M68k() { delete[] hookmap; }

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
uint8_t M68k::read8(uint32_t a) {
    a &= 0xFFFFFF;
    if (uint8_t* p = page_read[a >> 16]) return p[a & 0xFFFF];
    return rd8(ctx, a);
}
uint16_t M68k::read16(uint32_t a) {
    a &= 0xFFFFFE;
    if (uint8_t* p = page_read[a >> 16]) { p += a & 0xFFFF; return (uint16_t)((p[0] << 8) | p[1]); }
    return rd16(ctx, a);
}
uint32_t M68k::read32(uint32_t a) { return ((uint32_t)read16(a) << 16) | read16(a + 2); }
void M68k::write8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (uint8_t* p = page_write[a >> 16]) { p[a & 0xFFFF] = v; return; }
    wr8(ctx, a, v);
}
void M68k::write16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (uint8_t* p = page_write[a >> 16]) { p += a & 0xFFFF; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return; }
    wr16(ctx, a, v);
}
void M68k::write32(uint32_t a, uint32_t v) { write16(a, (uint16_t)(v >> 16)); write16(a + 2, (uint16_t)v); }

uint32_t M68k::readsz(uint32_t a, int sz) {
    return sz == 1 ? read8(a) : sz == 2 ? read16(a) : read32(a);
}
void M68k::writesz(uint32_t a, int sz, uint32_t v) {
    if (sz == 1) write8(a, (uint8_t)v); else if (sz == 2) write16(a, (uint16_t)v); else write32(a, v);
}

inline uint16_t M68k::fetch16() {
    uint32_t a = pc & 0xFFFFFE;
    pc += 2;
    if (uint8_t* p = page_read[a >> 16]) { p += a & 0xFFFF; return (uint16_t)((p[0] << 8) | p[1]); }
    return rd16(ctx, a);
}
inline uint32_t M68k::fetch32() { uint32_t hi = fetch16(); return (hi << 16) | fetch16(); }

// ---------------------------------------------------------------------------
// Effective addresses
// ---------------------------------------------------------------------------
uint32_t M68k::index_ea(uint32_t base) {
    uint16_t ext = fetch16();
    uint32_t idx = r[(ext >> 12) & 15];
    if (!(ext & 0x800)) idx = sx16(idx);
    return base + sx8(ext & 0xFF) + idx;
}

M68k::Ea M68k::getea(int mode, int reg, int sz) {
    Ea e; e.kind = EA_MEM; e.reg = reg; e.addr = 0; e.imm = 0;
    const bool l = sz == 4;
    switch (mode) {
    case 0: e.kind = EA_D; return e;
    case 1: e.kind = EA_A; return e;
    case 2: e.addr = r[8 + reg]; cyc += l ? 8 : 4; return e;
    case 3: e.addr = r[8 + reg]; r[8 + reg] += (reg == 7 && sz == 1) ? 2 : sz; cyc += l ? 8 : 4; return e;
    case 4: r[8 + reg] -= (reg == 7 && sz == 1) ? 2 : sz; e.addr = r[8 + reg]; cyc += l ? 10 : 6; return e;
    case 5: e.addr = r[8 + reg] + sx16(fetch16()); cyc += l ? 12 : 8; return e;
    case 6: e.addr = index_ea(r[8 + reg]); cyc += l ? 14 : 10; return e;
    default:
        switch (reg) {
        case 0: e.addr = sx16(fetch16()); cyc += l ? 12 : 8; return e;
        case 1: e.addr = fetch32(); cyc += l ? 16 : 12; return e;
        case 2: { uint32_t base = pc; e.addr = base + sx16(fetch16()); cyc += l ? 12 : 8; return e; }
        case 3: { uint32_t base = pc; e.addr = index_ea(base); cyc += l ? 14 : 10; return e; }
        case 4: e.kind = EA_IMM; e.imm = l ? fetch32() : (fetch16() & MASKS(sz)); cyc += l ? 8 : 4; return e;
        }
    }
    e.kind = EA_IMM;
    return e;
}

uint32_t M68k::rd(const Ea& e, int sz) {
    switch (e.kind) {
    case EA_D: return r[e.reg] & MASKS(sz);
    case EA_A: return r[8 + e.reg] & MASKS(sz);
    case EA_MEM: return readsz(e.addr, sz);
    default: return e.imm;
    }
}

void M68k::wr(const Ea& e, int sz, uint32_t v) {
    switch (e.kind) {
    case EA_D: { uint32_t m = MASKS(sz); r[e.reg] = (r[e.reg] & ~m) | (v & m); break; }
    case EA_A: r[8 + e.reg] = v; break;
    case EA_MEM: writesz(e.addr, sz, v); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Status register / exceptions
// ---------------------------------------------------------------------------
uint16_t M68k::get_sr() const {
    return (uint16_t)(srh | (fx << 4) | (fn << 3) | (fz << 2) | (fv << 1) | fc);
}
void M68k::set_ccr(uint8_t v) {
    fx = (v >> 4) & 1; fn = (v >> 3) & 1; fz = (v >> 2) & 1; fv = (v >> 1) & 1; fc = v & 1;
}
void M68k::set_sr(uint16_t v) {
    bool olds = (srh & 0x2000) != 0;
    srh = v & 0xA700;
    set_ccr((uint8_t)v);
    bool news = (srh & 0x2000) != 0;
    if (olds != news) {
        if (news) { usp = r[15]; r[15] = ssp; }
        else      { ssp = r[15]; r[15] = usp; }
    }
}
void M68k::set_nz(uint32_t v, int sz) {
    v &= MASKS(sz);
    fn = (v & MSBS(sz)) != 0;
    fz = v == 0;
}
bool M68k::cond(int c) const {
    switch (c) {
    case 0: return true;
    case 1: return false;
    case 2: return !fc && !fz;
    case 3: return fc || fz;
    case 4: return !fc;
    case 5: return fc != 0;
    case 6: return !fz;
    case 7: return fz != 0;
    case 8: return !fv;
    case 9: return fv != 0;
    case 10: return !fn;
    case 11: return fn != 0;
    case 12: return fn == fv;
    case 13: return fn != fv;
    case 14: return !fz && fn == fv;
    default: return fz || fn != fv;
    }
}
void M68k::exception(int vec, uint32_t ret_pc) {
    uint16_t old = get_sr();
    set_sr((uint16_t)((old | 0x2000) & ~0x8000));
    push32(ret_pc);
    push16(old);
    pc = read32(vec * 4);
    cyc += 34;
}
void M68k::take_irq(int level) {
    stopped = false;
    uint16_t old = get_sr();
    set_sr((uint16_t)(((old | 0x2000) & ~0x8700) | (level << 8)));
    int vec = irq_ack ? irq_ack(ctx, level) : -1;
    if (vec < 0) vec = 24 + level;
    push32(pc);
    push16(old);
    pc = read32(vec * 4);
    cyc += 44;
}

void M68k::reset() {
    stopped = false;
    srh = 0x2700;
    fx = fn = fz = fv = fc = 0;
    irq_level = 0;
    r[15] = ssp = read32(0);
    pc = read32(4);
    usp = 0;
}

void M68k::set_hook(uint32_t addr, bool on) {
    uint32_t wi = (addr & 0xFFFFFF) >> 1;
    if (on) hookmap[wi >> 3] |= (uint8_t)(1 << (wi & 7));
    else    hookmap[wi >> 3] &= (uint8_t)~(1 << (wi & 7));
    if (on) any_hooks = true;
}
void M68k::clear_hooks() { memset(hookmap, 0, 1 << 20); any_hooks = false; }

int M68k::run(int cycles) {
    cyc = 0;
    target = cycles;
    while (cyc < target) {
        if (irq_level > ((srh >> 8) & 7)) take_irq(irq_level);
        if (stopped) { if (cyc < target) cyc = target; break; }
        if (any_hooks) {
            uint32_t wi = (pc & 0xFFFFFF) >> 1;
            if (hookmap[wi >> 3] & (1 << (wi & 7))) {
                uint32_t before = pc;
                hook_fn(hook_ctx, pc);
                if (pc != before || cyc >= target) continue;
            }
        }
        if (trace_fn) {
            trace_fn(trace_ctx, pc);
            if (cyc >= target) break;
        }
        ppc = pc;
        uint32_t op = fetch16();
        optable[op](*this, op);
    }
    total_cycles += (uint64_t)cyc;
    return cyc;
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
struct M68kState {
    uint32_t r[16], usp, ssp, pc;
    uint16_t sr;
    uint8_t stopped, irq_level;
    uint64_t total_cycles;
};
size_t M68k::state_size() const { return sizeof(M68kState); }
void M68k::save_state(uint8_t* dst) const {
    M68kState s;
    memset(&s, 0, sizeof(s));
    memcpy(s.r, r, sizeof(r));
    s.usp = usp; s.ssp = ssp; s.pc = pc; s.sr = get_sr();
    s.stopped = stopped; s.irq_level = (uint8_t)irq_level; s.total_cycles = total_cycles;
    memcpy(dst, &s, sizeof(s));
}
void M68k::load_state(const uint8_t* src) {
    M68kState s;
    memcpy(&s, src, sizeof(s));
    memcpy(r, s.r, sizeof(r));
    usp = s.usp; ssp = s.ssp; pc = s.pc;
    srh = s.sr & 0xA700; set_ccr((uint8_t)s.sr);
    stopped = s.stopped != 0; irq_level = s.irq_level; total_cycles = s.total_cycles;
}

// ---------------------------------------------------------------------------
// ALU helpers
// ---------------------------------------------------------------------------
typedef M68k::Ea Ea;

static inline void logic_flags(M68k& c, uint32_t v, int sz) { c.set_nz(v, sz); c.fv = c.fc = 0; }

static inline uint32_t do_add(M68k& c, uint32_t s, uint32_t d, int sz) {
    uint32_t m = MASKS(sz), h = MSBS(sz);
    s &= m; d &= m;
    uint64_t r64 = (uint64_t)s + d;
    uint32_t res = (uint32_t)r64 & m;
    c.fc = c.fx = (uint32_t)((r64 >> (sz * 8)) & 1);
    c.fv = ((s ^ res) & (d ^ res) & h) != 0;
    c.fn = (res & h) != 0;
    c.fz = res == 0;
    return res;
}
static inline uint32_t do_sub(M68k& c, uint32_t s, uint32_t d, int sz) {   // d - s
    uint32_t m = MASKS(sz), h = MSBS(sz);
    s &= m; d &= m;
    uint32_t res = (d - s) & m;
    c.fc = c.fx = s > d;
    c.fv = ((s ^ d) & (res ^ d) & h) != 0;
    c.fn = (res & h) != 0;
    c.fz = res == 0;
    return res;
}
static inline void do_cmp(M68k& c, uint32_t s, uint32_t d, int sz) {
    uint32_t m = MASKS(sz), h = MSBS(sz);
    s &= m; d &= m;
    uint32_t res = (d - s) & m;
    c.fc = s > d;
    c.fv = ((s ^ d) & (res ^ d) & h) != 0;
    c.fn = (res & h) != 0;
    c.fz = res == 0;
}
static inline uint32_t do_addx(M68k& c, uint32_t s, uint32_t d, int sz) {
    uint32_t m = MASKS(sz), h = MSBS(sz);
    s &= m; d &= m;
    uint64_t r64 = (uint64_t)s + d + c.fx;
    uint32_t res = (uint32_t)r64 & m;
    c.fc = c.fx = (uint32_t)((r64 >> (sz * 8)) & 1);
    c.fv = ((s ^ res) & (d ^ res) & h) != 0;
    c.fn = (res & h) != 0;
    if (res) c.fz = 0;
    return res;
}
static inline uint32_t do_subx(M68k& c, uint32_t s, uint32_t d, int sz) {  // d - s - X
    uint32_t m = MASKS(sz), h = MSBS(sz);
    s &= m; d &= m;
    uint64_t sub = (uint64_t)s + c.fx;
    uint32_t res = (uint32_t)(d - sub) & m;
    c.fc = c.fx = sub > d;
    c.fv = ((s ^ d) & (res ^ d) & h) != 0;
    c.fn = (res & h) != 0;
    if (res) c.fz = 0;
    return res;
}
// BCD arithmetic including the undocumented N/V behaviour of the real chip.
static inline uint32_t do_abcd(M68k& c, uint32_t src, uint32_t dst) {
    uint32_t res = (src & 0x0F) + (dst & 0x0F) + c.fx;
    uint32_t v = ~res;
    if (res > 9) res += 6;
    res += (src & 0xF0) + (dst & 0xF0);
    c.fx = c.fc = res > 0x99;
    if (c.fc) res -= 0xA0;
    c.fv = (v & res & 0x80) != 0;
    c.fn = (res & 0x80) != 0;
    res &= 0xFF;
    if (res) c.fz = 0;
    return res;
}
static inline uint32_t do_sbcd(M68k& c, uint32_t src, uint32_t dst) {
    uint32_t res = (dst & 0x0F) - (src & 0x0F) - c.fx;
    uint32_t v = ~res;
    if (res > 9) res -= 6;
    res += (dst & 0xF0) - (src & 0xF0);
    c.fx = c.fc = res > 0x99;
    if (c.fc) res += 0xA0;
    res &= 0xFF;
    c.fv = (v & res & 0x80) != 0;
    c.fn = (res & 0x80) != 0;
    if (res) c.fz = 0;
    return res;
}

static uint32_t do_shift(M68k& c, int type, bool left, uint32_t v, int cnt, int sz) {
    uint32_t m = MASKS(sz), h = MSBS(sz);
    v &= m;
    uint32_t carry = 0;
    c.fv = 0;
    switch (type) {
    case 0: // ASL/ASR
        if (left) {
            for (int i = 0; i < cnt; i++) {
                carry = (v & h) != 0;
                uint32_t nv = (v << 1) & m;
                if ((nv ^ v) & h) c.fv = 1;
                v = nv;
            }
        } else {
            for (int i = 0; i < cnt; i++) { carry = v & 1; v = (v >> 1) | (v & h); }
        }
        if (cnt) c.fc = c.fx = carry; else c.fc = 0;
        break;
    case 1: // LSL/LSR
        if (left) { for (int i = 0; i < cnt; i++) { carry = (v & h) != 0; v = (v << 1) & m; } }
        else      { for (int i = 0; i < cnt; i++) { carry = v & 1; v >>= 1; } }
        if (cnt) c.fc = c.fx = carry; else c.fc = 0;
        break;
    case 2: // ROXL/ROXR
        for (int i = 0; i < cnt; i++) {
            if (left) { carry = (v & h) != 0; v = ((v << 1) | c.fx) & m; }
            else      { carry = v & 1; v = (v >> 1) | (c.fx ? h : 0); }
            c.fx = carry;
        }
        c.fc = c.fx;
        break;
    default: // ROL/ROR
        for (int i = 0; i < cnt; i++) {
            if (left) { carry = (v & h) != 0; v = ((v << 1) | carry) & m; }
            else      { carry = v & 1; v = (v >> 1) | (carry ? h : 0); }
        }
        c.fc = cnt ? carry : 0;
        break;
    }
    c.set_nz(v, sz);
    return v;
}

static inline bool is_imm_or_reg(int mode, int reg) { return mode <= 1 || (mode == 7 && reg == 4); }

// ---------------------------------------------------------------------------
// Opcode handlers
// ---------------------------------------------------------------------------
#define MODE ((op >> 3) & 7)
#define REG  (op & 7)
#define REG9 ((op >> 9) & 7)

static void op_illegal(M68k& c, uint32_t) { c.exception(4, c.ppc); }
static void op_linea(M68k& c, uint32_t) { c.exception(10, c.ppc); }
static void op_linef(M68k& c, uint32_t) { c.exception(11, c.ppc); }
static inline bool priv_check(M68k& c) {
    if (c.srh & 0x2000) return true;
    c.exception(8, c.ppc);
    return false;
}

// ORI/ANDI/SUBI/ADDI/EORI/CMPI #imm,<ea>
static void op_imm(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3);
    uint32_t imm = sz == 4 ? c.fetch32() : (c.fetch16() & MASKS(sz));
    int mode = MODE;
    Ea e = c.getea(mode, REG, sz);
    uint32_t d = c.rd(e, sz), res;
    switch ((op >> 9) & 7) {
    case 0: res = (d | imm) & MASKS(sz); logic_flags(c, res, sz); break;
    case 1: res = (d & imm) & MASKS(sz); logic_flags(c, res, sz); break;
    case 2: res = do_sub(c, imm, d, sz); break;
    case 3: res = do_add(c, imm, d, sz); break;
    case 5: res = (d ^ imm) & MASKS(sz); logic_flags(c, res, sz); break;
    default:
        do_cmp(c, imm, d, sz);
        c.cyc += mode == 0 ? (sz == 4 ? 14 : 8) : (sz == 4 ? 12 : 8);
        return;
    }
    c.wr(e, sz, res);
    c.cyc += mode == 0 ? (sz == 4 ? 16 : 8) : (sz == 4 ? 20 : 12);
}

static void op_ori_ccr(M68k& c, uint32_t)  { c.set_ccr((uint8_t)((c.get_sr() | c.fetch16()) & 0x1F)); c.cyc += 20; }
static void op_andi_ccr(M68k& c, uint32_t) { c.set_ccr((uint8_t)((c.get_sr() & c.fetch16()) & 0x1F)); c.cyc += 20; }
static void op_eori_ccr(M68k& c, uint32_t) { c.set_ccr((uint8_t)((c.get_sr() ^ c.fetch16()) & 0x1F)); c.cyc += 20; }
static void op_ori_sr(M68k& c, uint32_t)  { if (!priv_check(c)) return; uint16_t v = c.fetch16(); c.set_sr(c.get_sr() | v); c.cyc += 20; }
static void op_andi_sr(M68k& c, uint32_t) { if (!priv_check(c)) return; uint16_t v = c.fetch16(); c.set_sr(c.get_sr() & v); c.cyc += 20; }
static void op_eori_sr(M68k& c, uint32_t) { if (!priv_check(c)) return; uint16_t v = c.fetch16(); c.set_sr(c.get_sr() ^ v); c.cyc += 20; }

// BTST/BCHG/BCLR/BSET, dynamic (Dn) and static (#imm) bit number
static void op_bit(M68k& c, uint32_t op) {
    int type = (op >> 6) & 3;
    bool stat = !(op & 0x100);
    uint32_t bit = stat ? (c.fetch16() & 0xFF) : c.r[REG9];
    int mode = MODE;
    if (mode == 0) {
        bit &= 31;
        uint32_t v = c.r[REG];
        c.fz = !((v >> bit) & 1);
        if (type == 1) v ^= 1u << bit; else if (type == 2) v &= ~(1u << bit); else if (type == 3) v |= 1u << bit;
        c.r[REG] = v;
        c.cyc += (type == 0 ? 6 : type == 2 ? 10 : 8) + (stat ? 4 : 0);
    } else {
        bit &= 7;
        Ea e = c.getea(mode, REG, 1);
        uint32_t v = c.rd(e, 1);
        c.fz = !((v >> bit) & 1);
        if (type) {
            if (type == 1) v ^= 1u << bit; else if (type == 2) v &= ~(1u << bit); else v |= 1u << bit;
            c.wr(e, 1, v);
        }
        c.cyc += (type == 0 ? 4 : 8) + (stat ? 4 : 0);
    }
}

static void op_movep(M68k& c, uint32_t op) {
    uint32_t a = c.r[8 + REG] + sx16(c.fetch16());
    uint32_t& d = c.r[REG9];
    switch ((op >> 6) & 3) {
    case 0: d = (d & 0xFFFF0000) | ((uint32_t)c.read8(a) << 8) | c.read8(a + 2); c.cyc += 16; break;
    case 1: d = ((uint32_t)c.read8(a) << 24) | ((uint32_t)c.read8(a + 2) << 16) | ((uint32_t)c.read8(a + 4) << 8) | c.read8(a + 6); c.cyc += 24; break;
    case 2: c.write8(a, (uint8_t)(d >> 8)); c.write8(a + 2, (uint8_t)d); c.cyc += 16; break;
    default: c.write8(a, (uint8_t)(d >> 24)); c.write8(a + 2, (uint8_t)(d >> 16)); c.write8(a + 4, (uint8_t)(d >> 8)); c.write8(a + 6, (uint8_t)d); c.cyc += 24; break;
    }
}

static void op_move(M68k& c, uint32_t op) {
    int s = (op >> 12) & 3;
    int sz = s == 1 ? 1 : s == 3 ? 2 : 4;
    Ea src = c.getea(MODE, REG, sz);
    uint32_t v = c.rd(src, sz);
    int dmode = (op >> 6) & 7;
    if (dmode == 1) { c.r[8 + REG9] = sxs(v, sz); c.cyc += 4; return; }   // MOVEA
    Ea dst = c.getea(dmode, REG9, sz);
    if (dmode == 4) c.cyc -= 2;
    c.wr(dst, sz, v);
    logic_flags(c, v, sz);
    c.cyc += 4;
}

// ---- line 4 ----
static void op_negx(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3); int mode = MODE;
    Ea e = c.getea(mode, REG, sz);
    c.wr(e, sz, do_subx(c, c.rd(e, sz), 0, sz));
    c.cyc += mode == 0 ? (sz == 4 ? 6 : 4) : (sz == 4 ? 12 : 8);
}
static void op_clr(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3); int mode = MODE;
    Ea e = c.getea(mode, REG, sz);
    c.wr(e, sz, 0);
    c.fn = 0; c.fz = 1; c.fv = 0; c.fc = 0;
    c.cyc += mode == 0 ? (sz == 4 ? 6 : 4) : (sz == 4 ? 12 : 8);
}
static void op_neg(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3); int mode = MODE;
    Ea e = c.getea(mode, REG, sz);
    c.wr(e, sz, do_sub(c, c.rd(e, sz), 0, sz));
    c.cyc += mode == 0 ? (sz == 4 ? 6 : 4) : (sz == 4 ? 12 : 8);
}
static void op_not(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3); int mode = MODE;
    Ea e = c.getea(mode, REG, sz);
    uint32_t res = ~c.rd(e, sz) & MASKS(sz);
    c.wr(e, sz, res);
    logic_flags(c, res, sz);
    c.cyc += mode == 0 ? (sz == 4 ? 6 : 4) : (sz == 4 ? 12 : 8);
}
static void op_move_from_sr(M68k& c, uint32_t op) {
    int mode = MODE;
    Ea e = c.getea(mode, REG, 2);
    c.wr(e, 2, c.get_sr());
    c.cyc += mode == 0 ? 6 : 8;
}
static void op_move_to_ccr(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    c.set_ccr((uint8_t)(c.rd(e, 2) & 0x1F));
    c.cyc += 12;
}
static void op_move_to_sr(M68k& c, uint32_t op) {
    if (!priv_check(c)) return;
    Ea e = c.getea(MODE, REG, 2);
    c.set_sr((uint16_t)c.rd(e, 2));
    c.cyc += 12;
}
static void op_nbcd(M68k& c, uint32_t op) {
    int mode = MODE;
    Ea e = c.getea(mode, REG, 1);
    uint32_t dst = c.rd(e, 1);
    uint32_t res = (0x9A - dst - c.fx) & 0xFF;
    if (res != 0x9A) {
        uint32_t v = ~res;
        if ((res & 0x0F) == 0x0A) res = (res & 0xF0) + 0x10;
        res &= 0xFF;
        c.fv = (v & res & 0x80) != 0;
        c.wr(e, 1, res);
        if (res) c.fz = 0;
        c.fc = c.fx = 1;
    } else {
        c.fv = 0; c.fc = c.fx = 0;
    }
    c.fn = (res & 0x80) != 0;
    c.cyc += mode == 0 ? 6 : 8;
}
static void op_swap(M68k& c, uint32_t op) {
    uint32_t& d = c.r[REG];
    d = (d >> 16) | (d << 16);
    logic_flags(c, d, 4);
    c.cyc += 4;
}
static void op_pea(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    c.push32(e.addr);
    c.cyc += 8;
}
static void op_extw(M68k& c, uint32_t op) {
    uint32_t& d = c.r[REG];
    d = (d & 0xFFFF0000) | (sx8(d) & 0xFFFF);
    logic_flags(c, d, 2);
    c.cyc += 4;
}
static void op_extl(M68k& c, uint32_t op) {
    uint32_t& d = c.r[REG];
    d = sx16(d);
    logic_flags(c, d, 4);
    c.cyc += 4;
}
static void op_movem(M68k& c, uint32_t op) {
    int sz = (op & 0x40) ? 4 : 2;
    uint16_t mask = c.fetch16();
    int mode = MODE, reg = REG;
    int n = 0;
    if (!(op & 0x400)) {            // registers -> memory
        if (mode == 4) {
            uint32_t a = c.r[8 + reg];
            for (int i = 0; i < 16; i++)
                if (mask & (1 << i)) { a -= sz; c.writesz(a, sz, c.r[15 - i]); n++; }
            c.r[8 + reg] = a;
        } else {
            uint32_t a = c.getea(mode, reg, 2).addr;
            for (int i = 0; i < 16; i++)
                if (mask & (1 << i)) { c.writesz(a, sz, c.r[i]); a += sz; n++; }
        }
        c.cyc += 4 + n * (sz == 4 ? 8 : 4);
    } else {                        // memory -> registers
        uint32_t a = mode == 3 ? c.r[8 + reg] : c.getea(mode, reg, 2).addr;
        for (int i = 0; i < 16; i++)
            if (mask & (1 << i)) {
                uint32_t v = c.readsz(a, sz);
                c.r[i] = sz == 2 ? sx16(v) : v;
                a += sz; n++;
            }
        if (mode == 3) c.r[8 + reg] = a;
        c.cyc += 12 + n * (sz == 4 ? 8 : 4);
    }
}
static void op_tst(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3);
    Ea e = c.getea(MODE, REG, sz);
    logic_flags(c, c.rd(e, sz), sz);
    c.cyc += 4;
}
static void op_tas(M68k& c, uint32_t op) {
    int mode = MODE;
    Ea e = c.getea(mode, REG, 1);
    uint32_t v = c.rd(e, 1);
    logic_flags(c, v, 1);
    // The Mega Drive bus does not complete TAS read-modify-write cycles to memory.
    if (mode == 0) c.wr(e, 1, v | 0x80);
    c.cyc += mode == 0 ? 4 : 14;
}
static void op_trap(M68k& c, uint32_t op) { c.exception(32 + (op & 15), c.pc); c.cyc += 4; }
static void op_link(M68k& c, uint32_t op) {
    int reg = REG;
    uint32_t disp = sx16(c.fetch16());
    c.push32(c.r[8 + reg]);
    c.r[8 + reg] = c.r[15];
    c.r[15] += disp;
    c.cyc += 16;
}
static void op_unlk(M68k& c, uint32_t op) {
    int reg = REG;
    c.r[15] = c.r[8 + reg];
    c.r[8 + reg] = c.pop32();
    c.cyc += 12;
}
static void op_move_usp(M68k& c, uint32_t op) {
    if (!priv_check(c)) return;
    if (op & 8) c.r[8 + REG] = c.usp; else c.usp = c.r[8 + REG];
    c.cyc += 4;
}
static void op_reset(M68k& c, uint32_t) { if (!priv_check(c)) return; c.cyc += 132; }
static void op_nop(M68k& c, uint32_t) { c.cyc += 4; }
static void op_stop(M68k& c, uint32_t) {
    if (!priv_check(c)) return;
    uint16_t v = c.fetch16();
    c.set_sr(v);
    c.stopped = true;
    c.cyc += 4;
}
static void op_rte(M68k& c, uint32_t) {
    if (!priv_check(c)) return;
    uint16_t sr = c.pop16();
    uint32_t npc = c.pop32();
    c.set_sr(sr);
    c.pc = npc;
    c.cyc += 20;
}
static void op_rts(M68k& c, uint32_t) { c.pc = c.pop32(); c.cyc += 16; }
static void op_trapv(M68k& c, uint32_t) { if (c.fv) c.exception(7, c.pc); c.cyc += 4; }
static void op_rtr(M68k& c, uint32_t) {
    c.set_ccr((uint8_t)(c.pop16() & 0x1F));
    c.pc = c.pop32();
    c.cyc += 20;
}
static void op_jsr(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    c.push32(c.pc);
    c.pc = e.addr;
    c.cyc += 12;
}
static void op_jmp(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    c.pc = e.addr;
    c.cyc += 4;
}
static void op_lea(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    c.r[8 + REG9] = e.addr;
}
static void op_chk(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    int16_t bound = (int16_t)c.rd(e, 2);
    int16_t v = (int16_t)c.r[REG9];
    c.cyc += 10;
    if (v < 0) { c.fn = 1; c.exception(6, c.pc); }
    else if (v > bound) { c.fn = 0; c.exception(6, c.pc); }
}

// ---- line 5 ----
static void op_addq_subq(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3);
    uint32_t data = REG9; if (!data) data = 8;
    int mode = MODE, reg = REG;
    bool sub = (op & 0x100) != 0;
    if (mode == 1) {
        c.r[8 + reg] += sub ? (uint32_t)(0u - data) : data;
        c.cyc += 8;
        return;
    }
    Ea e = c.getea(mode, reg, sz);
    uint32_t d = c.rd(e, sz);
    c.wr(e, sz, sub ? do_sub(c, data, d, sz) : do_add(c, data, d, sz));
    c.cyc += mode == 0 ? (sz == 4 ? 8 : 4) : (sz == 4 ? 12 : 8);
}
static void op_scc(M68k& c, uint32_t op) {
    int mode = MODE;
    Ea e = c.getea(mode, REG, 1);
    bool t = c.cond((op >> 8) & 15);
    c.wr(e, 1, t ? 0xFF : 0);
    c.cyc += mode == 0 ? (t ? 6 : 4) : 8;
}
static void op_dbcc(M68k& c, uint32_t op) {
    uint32_t base = c.pc;
    uint32_t disp = sx16(c.fetch16());
    if (c.cond((op >> 8) & 15)) { c.cyc += 12; return; }
    uint32_t& d = c.r[REG];
    uint16_t cnt = (uint16_t)(d - 1);
    d = (d & 0xFFFF0000) | cnt;
    if (cnt != 0xFFFF) { c.pc = base + disp; c.cyc += 10; }
    else c.cyc += 14;
}

// ---- line 6 ----
static void op_bcc(M68k& c, uint32_t op) {
    uint32_t base = c.pc;
    uint32_t disp = sx8(op & 0xFF);
    bool word = (op & 0xFF) == 0;
    if (word) disp = sx16(c.fetch16());
    int cc = (op >> 8) & 15;
    if (cc == 1) {  // BSR
        c.push32(c.pc);
        c.pc = base + disp;
        c.cyc += 18;
        return;
    }
    if (c.cond(cc)) { c.pc = base + disp; c.cyc += 10; }
    else c.cyc += word ? 12 : 8;
}

static void op_moveq(M68k& c, uint32_t op) {
    uint32_t v = sx8(op & 0xFF);
    c.r[REG9] = v;
    logic_flags(c, v, 4);
    c.cyc += 4;
}

// ---- OR / AND / EOR ----
template <int K>  // 0 = OR, 1 = AND
static void op_orand(M68k& c, uint32_t op) {
    int opm = (op >> 6) & 7;
    int sz = size_std(opm & 3);
    int mode = MODE, reg = REG, dn = REG9;
    Ea e = c.getea(mode, reg, sz);
    uint32_t s = c.rd(e, sz), d = c.r[dn];
    uint32_t res = (K ? (s & d) : (s | d)) & MASKS(sz);
    if (opm < 4) {
        c.r[dn] = (d & ~MASKS(sz)) | res;
        c.cyc += sz == 4 ? (is_imm_or_reg(mode, reg) ? 8 : 6) : 4;
    } else {
        c.wr(e, sz, res);
        c.cyc += sz == 4 ? 12 : 8;
    }
    logic_flags(c, res, sz);
}

static void op_divu(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    uint32_t src = c.rd(e, 2);
    int dn = REG9;
    if (src == 0) { c.fc = 0; c.exception(5, c.pc); return; }
    uint32_t dividend = c.r[dn];
    uint32_t q = dividend / src, rem = dividend % src;
    c.fc = 0;
    if (q > 0xFFFF) { c.fv = 1; c.fn = 1; c.cyc += 10; return; }
    c.r[dn] = (rem << 16) | q;
    c.fn = (q >> 15) & 1; c.fz = q == 0; c.fv = 0;
    c.cyc += 126;
}
static void op_divs(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    int32_t src = (int16_t)c.rd(e, 2);
    int dn = REG9;
    if (src == 0) { c.fc = 0; c.exception(5, c.pc); return; }
    int32_t dividend = (int32_t)c.r[dn];
    c.fc = 0;
    if (dividend == INT32_MIN && src == -1) { c.fv = 1; c.fn = 1; c.cyc += 16; return; }
    int32_t q = dividend / src, rem = dividend % src;
    if (q < -32768 || q > 32767) { c.fv = 1; c.fn = 1; c.cyc += 16; return; }
    c.r[dn] = ((uint32_t)(rem & 0xFFFF) << 16) | (uint32_t)(q & 0xFFFF);
    c.fn = (q & 0x8000) != 0; c.fz = (q & 0xFFFF) == 0; c.fv = 0;
    c.cyc += 146;
}
static void op_mulu(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    uint32_t s = c.rd(e, 2);
    int dn = REG9;
    uint32_t res = (c.r[dn] & 0xFFFF) * s;
    c.r[dn] = res;
    logic_flags(c, res, 4);
    int ones = 0; for (uint32_t t = s; t; t &= t - 1) ones++;
    c.cyc += 38 + 2 * ones;
}
static void op_muls(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    uint32_t s = c.rd(e, 2);
    int dn = REG9;
    uint32_t res = (uint32_t)((int32_t)(int16_t)c.r[dn] * (int32_t)(int16_t)s);
    c.r[dn] = res;
    logic_flags(c, res, 4);
    int n = 0; for (uint32_t t = (s << 1) ^ s; t & 0x1FFFF; t &= t - 1) n++;
    c.cyc += 38 + 2 * n;
}

// ---- ADD / SUB ----
template <bool SUB>
static void op_addsub(M68k& c, uint32_t op) {
    int opm = (op >> 6) & 7;
    int mode = MODE, reg = REG, dn = REG9;
    if (opm == 3 || opm == 7) {     // ADDA / SUBA
        int sz = opm == 3 ? 2 : 4;
        Ea e = c.getea(mode, reg, sz);
        uint32_t s = sxs(c.rd(e, sz), sz);
        c.r[8 + dn] = SUB ? c.r[8 + dn] - s : c.r[8 + dn] + s;
        c.cyc += (sz == 4 && !is_imm_or_reg(mode, reg)) ? 6 : 8;
        return;
    }
    int sz = size_std(opm & 3);
    Ea e = c.getea(mode, reg, sz);
    if (opm < 4) {
        uint32_t s = c.rd(e, sz), d = c.r[dn];
        uint32_t res = SUB ? do_sub(c, s, d, sz) : do_add(c, s, d, sz);
        c.r[dn] = (d & ~MASKS(sz)) | res;
        c.cyc += sz == 4 ? (is_imm_or_reg(mode, reg) ? 8 : 6) : 4;
    } else {
        uint32_t d = c.rd(e, sz), s = c.r[dn];
        c.wr(e, sz, SUB ? do_sub(c, s, d, sz) : do_add(c, s, d, sz));
        c.cyc += sz == 4 ? 12 : 8;
    }
}
template <bool SUB>
static void op_addsubx(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3);
    int rx = REG9, ry = REG;
    if (op & 8) {
        Ea es = c.getea(4, ry, sz); uint32_t s = c.rd(es, sz);
        Ea ed = c.getea(4, rx, sz); uint32_t d = c.rd(ed, sz);
        c.wr(ed, sz, SUB ? do_subx(c, s, d, sz) : do_addx(c, s, d, sz));
        c.cyc += sz == 4 ? 10 : 6;
    } else {
        uint32_t s = c.r[ry], d = c.r[rx];
        uint32_t res = SUB ? do_subx(c, s, d, sz) : do_addx(c, s, d, sz);
        c.r[rx] = (d & ~MASKS(sz)) | res;
        c.cyc += sz == 4 ? 8 : 4;
    }
}
template <bool SUB>
static void op_bcd(M68k& c, uint32_t op) {
    int rx = REG9, ry = REG;
    if (op & 8) {
        Ea es = c.getea(4, ry, 1); uint32_t s = c.rd(es, 1);
        Ea ed = c.getea(4, rx, 1); uint32_t d = c.rd(ed, 1);
        c.wr(ed, 1, SUB ? do_sbcd(c, s, d) : do_abcd(c, s, d));
        c.cyc += 6;
    } else {
        uint32_t s = c.r[ry] & 0xFF, d = c.r[rx] & 0xFF;
        uint32_t res = SUB ? do_sbcd(c, s, d) : do_abcd(c, s, d);
        c.r[rx] = (c.r[rx] & ~0xFFu) | res;
        c.cyc += 6;
    }
}

// ---- CMP / CMPA / CMPM / EOR ----
static void op_cmp_eor(M68k& c, uint32_t op) {
    int opm = (op >> 6) & 7;
    int mode = MODE, reg = REG, dn = REG9;
    if (opm == 3 || opm == 7) {     // CMPA
        int sz = opm == 3 ? 2 : 4;
        Ea e = c.getea(mode, reg, sz);
        do_cmp(c, sxs(c.rd(e, sz), sz), c.r[8 + dn], 4);
        c.cyc += 6;
        return;
    }
    int sz = size_std(opm & 3);
    if (opm < 4) {                  // CMP <ea>,Dn
        Ea e = c.getea(mode, reg, sz);
        do_cmp(c, c.rd(e, sz), c.r[dn], sz);
        c.cyc += sz == 4 ? 6 : 4;
        return;
    }
    if (mode == 1) {                // CMPM (Ay)+,(Ax)+
        Ea es = c.getea(3, reg, sz); uint32_t s = c.rd(es, sz);
        Ea ed = c.getea(3, dn, sz);  uint32_t d = c.rd(ed, sz);
        do_cmp(c, s, d, sz);
        c.cyc += 4;
        return;
    }
    Ea e = c.getea(mode, reg, sz);   // EOR Dn,<ea>
    uint32_t res = (c.rd(e, sz) ^ c.r[dn]) & MASKS(sz);
    c.wr(e, sz, res);
    logic_flags(c, res, sz);
    c.cyc += mode == 0 ? (sz == 4 ? 8 : 4) : (sz == 4 ? 12 : 8);
}

static void op_exg(M68k& c, uint32_t op) {
    int rx = REG9, ry = REG;
    switch ((op >> 3) & 0x1F) {
    case 0x08: std::swap(c.r[rx], c.r[ry]); break;
    case 0x09: std::swap(c.r[8 + rx], c.r[8 + ry]); break;
    default:   std::swap(c.r[rx], c.r[8 + ry]); break;
    }
    c.cyc += 6;
}

// ---- shifts / rotates ----
static void op_shift_reg(M68k& c, uint32_t op) {
    int sz = size_std((op >> 6) & 3);
    int cnt = REG9;
    if (op & 0x20) cnt = c.r[cnt] & 63; else if (cnt == 0) cnt = 8;
    int reg = REG;
    uint32_t res = do_shift(c, (op >> 3) & 3, (op & 0x100) != 0, c.r[reg], cnt, sz);
    c.r[reg] = (c.r[reg] & ~MASKS(sz)) | res;
    c.cyc += (sz == 4 ? 8 : 6) + 2 * cnt;
}
static void op_shift_mem(M68k& c, uint32_t op) {
    Ea e = c.getea(MODE, REG, 2);
    uint32_t v = c.rd(e, 2);
    c.wr(e, 2, do_shift(c, (op >> 9) & 3, (op & 0x100) != 0, v, 1, 2));
    c.cyc += 8;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
static M68k::OpFn decode(uint32_t op) {
    int mode = MODE;
    int opm = (op >> 6) & 7;
    int sz2 = (op >> 6) & 3;
    switch (op >> 12) {
    case 0x0:
        switch (op) {
        case 0x003C: return op_ori_ccr;
        case 0x007C: return op_ori_sr;
        case 0x023C: return op_andi_ccr;
        case 0x027C: return op_andi_sr;
        case 0x0A3C: return op_eori_ccr;
        case 0x0A7C: return op_eori_sr;
        }
        if (op & 0x100) return mode == 1 ? op_movep : op_bit;
        switch ((op >> 9) & 7) {
        case 4: return op_bit;
        case 7: return op_illegal;
        default: return sz2 == 3 ? op_illegal : op_imm;
        }
    case 0x1: case 0x2: case 0x3:
        return op_move;
    case 0x4:
        if (op & 0x100) {
            if (opm == 7) return op_lea;
            if (opm == 6) return op_chk;
            return op_illegal;
        }
        switch ((op >> 8) & 0xF) {
        case 0x0: return sz2 == 3 ? op_move_from_sr : op_negx;
        case 0x2: return sz2 == 3 ? op_illegal : op_clr;
        case 0x4: return sz2 == 3 ? op_move_to_ccr : op_neg;
        case 0x6: return sz2 == 3 ? op_move_to_sr : op_not;
        case 0x8:
            switch (sz2) {
            case 0: return op_nbcd;
            case 1: return mode == 0 ? op_swap : op_pea;
            case 2: return mode == 0 ? op_extw : op_movem;
            default: return mode == 0 ? op_extl : op_movem;
            }
        case 0xA:
            if (op == 0x4AFC) return op_illegal;
            return sz2 == 3 ? op_tas : op_tst;
        case 0xC:
            return sz2 >= 2 ? op_movem : op_illegal;
        case 0xE:
            if (sz2 == 1) {
                switch ((op >> 3) & 7) {
                case 0: case 1: return op_trap;
                case 2: return op_link;
                case 3: return op_unlk;
                case 4: case 5: return op_move_usp;
                case 6:
                    switch (op & 7) {
                    case 0: return op_reset;
                    case 1: return op_nop;
                    case 2: return op_stop;
                    case 3: return op_rte;
                    case 5: return op_rts;
                    case 6: return op_trapv;
                    case 7: return op_rtr;
                    default: return op_illegal;
                    }
                default: return op_illegal;
                }
            }
            if (sz2 == 2) return op_jsr;
            if (sz2 == 3) return op_jmp;
            return op_illegal;
        default:
            return op_illegal;
        }
    case 0x5:
        if (sz2 == 3) return mode == 1 ? op_dbcc : op_scc;
        return op_addq_subq;
    case 0x6:
        return op_bcc;
    case 0x7:
        return (op & 0x100) ? op_illegal : op_moveq;
    case 0x8:
        if (opm == 3) return op_divu;
        if (opm == 7) return op_divs;
        if (opm == 4 && mode < 2) return op_bcd<true>;
        return op_orand<0>;
    case 0x9:
        if (opm == 3 || opm == 7) return op_addsub<true>;
        if (opm >= 4 && mode < 2) return op_addsubx<true>;
        return op_addsub<true>;
    case 0xA:
        return op_linea;
    case 0xB:
        return op_cmp_eor;
    case 0xC:
        if (opm == 3) return op_mulu;
        if (opm == 7) return op_muls;
        if (opm == 4 && mode < 2) return op_bcd<false>;
        if (opm == 5 && mode < 2) return op_exg;
        if (opm == 6 && mode == 1) return op_exg;
        return op_orand<1>;
    case 0xD:
        if (opm == 3 || opm == 7) return op_addsub<false>;
        if (opm >= 4 && mode < 2) return op_addsubx<false>;
        return op_addsub<false>;
    case 0xE:
        if (sz2 == 3) return (op & 0x800) ? op_illegal : op_shift_mem;
        return op_shift_reg;
    default:
        return op_linef;
    }
}

void M68k::build_table() {
    static bool built = false;
    if (built) return;
    for (uint32_t op = 0; op < 65536; op++) optable[op] = decode(op);
    built = true;
}
