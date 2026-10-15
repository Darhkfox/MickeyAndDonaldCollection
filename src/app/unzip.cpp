#include "unzip.h"
#include "romfind.h"
#include <algorithm>
#include <fstream>

// ---------------------------------------------------------------------------
// Inflate (after Mark Adler's puff.c: canonical Huffman decoding a bit at a time, which is plenty
// for a few megabytes of ROM)
// ---------------------------------------------------------------------------
namespace {
const int kMaxBits = 15;

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

struct Inflater {
    const uint8_t* in;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool err = false;
    std::vector<uint8_t>& out;
    size_t limit;

    Inflater(const uint8_t* i, size_t len, std::vector<uint8_t>& o, size_t lim) : in(i), n(len), out(o), limit(lim) {}

    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= n) { err = true; return 0; }
            val |= (uint32_t)in[pos++] << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return (int)(val & ((1u << need) - 1));
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            code |= bits(1);
            if (err) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool stored() {
        bitbuf = 0;
        bitcnt = 0;
        if (pos + 4 > n) return false;
        unsigned len = in[pos] | (in[pos + 1] << 8);
        unsigned nlen = in[pos + 2] | (in[pos + 3] << 8);
        pos += 4;
        if (len != (~nlen & 0xFFFF) || pos + len > n || out.size() + len > limit) return false;
        out.insert(out.end(), in + pos, in + pos + len);
        pos += len;
        return true;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode) {
        static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                       7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 0) return false;
            if (symbol < 256) {
                if (out.size() >= limit) return false;
                out.push_back((uint8_t)symbol);
            } else if (symbol == 256) {
                return true;
            } else {
                symbol -= 257;
                if (symbol >= 29) return false;
                size_t len = lens[symbol] + bits(lext[symbol]);
                int d = decode(distcode);
                if (d < 0 || d >= 30) return false;
                size_t dist = dists[d] + bits(dext[d]);
                if (err || dist > out.size() || out.size() + len > limit) return false;
                size_t from = out.size() - dist;
                for (size_t k = 0; k < len; k++) out.push_back(out[from + k]);
            }
        }
    }
};

// Returns 0 for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed one.
int construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= kMaxBits; len++) h.count[len] = 0;
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = (short)s;
    return left;
}

bool fixed_block(Inflater& s) {
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built) {
        short lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        construct(lencode, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        construct(distcode, lengths, 30);
        built = true;
    }
    return s.codes(lencode, distcode);
}

bool dynamic_block(Inflater& s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[286 + 30];
    Huffman lencode, distcode;
    int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
    if (s.err || nlen > 286 || ndist > 30) return false;
    int index = 0;
    for (; index < ncode; index++) lengths[order[index]] = (short)s.bits(3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s.err || construct(lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = s.decode(lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            short len = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                symbol = 3 + s.bits(2);
            } else if (symbol == 17) {
                symbol = 3 + s.bits(3);
            } else {
                symbol = 11 + s.bits(7);
            }
            if (s.err || index + symbol > nlen + ndist) return false;
            while (symbol--) lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) return false;
    int e = construct(lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen != lencode.count[0] + lencode.count[1])) return false;
    e = construct(distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist != distcode.count[0] + distcode.count[1])) return false;
    return s.codes(lencode, distcode);
}

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

const uint32_t kMaxEntry = 64u << 20;   // nothing a Mega Drive ROM needs is bigger
}  // namespace

bool inflate_raw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    out.reserve(expected);
    Inflater s(in, n, out, expected);
    int last;
    do {
        last = s.bits(1);
        int type = s.bits(2);
        if (s.err) return false;
        bool ok = type == 0 ? s.stored() : type == 1 ? fixed_block(s) : type == 2 ? dynamic_block(s) : false;
        if (!ok || s.err) return false;
    } while (!last);
    return out.size() == expected;
}

// ---------------------------------------------------------------------------
// Zip archives
// ---------------------------------------------------------------------------
bool zip_list(const std::wstring& path, std::vector<ZipEntry>& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const uint64_t fsize = (uint64_t)f.tellg();
    if (fsize < 22) return false;
    // End of central directory record: within the last 64 KB + 22 bytes (it can be followed by a comment).
    const size_t tail = (size_t)std::min<uint64_t>(fsize, 65557);
    std::vector<uint8_t> t(tail);
    f.seekg((std::streamoff)(fsize - tail));
    if (!f.read((char*)t.data(), tail)) return false;
    size_t eocd = SIZE_MAX;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (rd32(&t[i]) == 0x06054B50) { eocd = i; break; }
    if (eocd == SIZE_MAX) return false;
    const uint16_t count = rd16(&t[eocd + 10]);
    const uint32_t cd_size = rd32(&t[eocd + 12]), cd_off = rd32(&t[eocd + 16]);
    if (cd_off == 0xFFFFFFFF || (uint64_t)cd_off + cd_size > fsize || cd_size > (16u << 20)) return false;
    std::vector<uint8_t> cd(cd_size);
    f.seekg(cd_off);
    if (cd_size && !f.read((char*)cd.data(), cd_size)) return false;
    size_t p = 0;
    for (unsigned k = 0; k < count && p + 46 <= cd.size(); k++) {
        if (rd32(&cd[p]) != 0x02014B50) return false;
        ZipEntry e;
        e.flags = rd16(&cd[p + 8]);
        e.method = rd16(&cd[p + 10]);
        e.crc = rd32(&cd[p + 16]);
        e.comp_size = rd32(&cd[p + 20]);
        e.size = rd32(&cd[p + 24]);
        const uint16_t nlen = rd16(&cd[p + 28]), xlen = rd16(&cd[p + 30]), clen = rd16(&cd[p + 32]);
        e.local_offset = rd32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) return false;
        e.name.assign((const char*)&cd[p + 46], nlen);
        p += 46 + (size_t)nlen + xlen + clen;
        if (!e.name.empty() && e.name.back() != '/') out.push_back(std::move(e));
    }
    return true;
}

bool zip_read(const std::wstring& path, const ZipEntry& e, std::vector<uint8_t>& out) {
    out.clear();
    if ((e.flags & 1) || e.size > kMaxEntry || e.comp_size > kMaxEntry) return false;   // encrypted / too big
    if (e.method != 0 && e.method != 8) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t lh[30];
    f.seekg(e.local_offset);
    if (!f.read((char*)lh, 30) || rd32(lh) != 0x04034B50) return false;
    f.seekg((std::streamoff)e.local_offset + 30 + rd16(&lh[26]) + rd16(&lh[28]));
    std::vector<uint8_t> comp(e.comp_size);
    if (e.comp_size && !f.read((char*)comp.data(), e.comp_size)) return false;
    if (e.method == 0) {
        if (e.comp_size != e.size) return false;
        out = std::move(comp);
    } else if (!inflate_raw(comp.data(), comp.size(), out, e.size)) {
        return false;
    }
    return crc32_of(out.data(), out.size()) == e.crc;
}
