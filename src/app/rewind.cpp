#include "rewind.h"

namespace {

void put_varint(std::vector<uint8_t>& out, size_t v) {
    while (v >= 0x80) { out.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    out.push_back((uint8_t)v);
}

size_t get_varint(const std::vector<uint8_t>& in, size_t& pos) {
    size_t v = 0;
    for (int shift = 0; pos < in.size(); shift += 7) {
        uint8_t b = in[pos++];
        v |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
    }
    return v;
}

// a XOR b as {zero run, literal length, literal bytes} records. Both have the same size.
std::vector<uint8_t> encode(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out;
    const size_t n = a.size();
    size_t i = 0;
    while (i < n) {
        size_t z = i;
        while (z < n && a[z] == b[z]) z++;
        size_t l = z;
        // a literal ends at the next run of 4+ equal bytes
        while (l < n) {
            if (a[l] == b[l]) {
                size_t r = l;
                while (r < n && r - l < 4 && a[r] == b[r]) r++;
                if (r - l >= 4 || r == n) break;
                l = r;
            } else {
                l++;
            }
        }
        put_varint(out, z - i);
        put_varint(out, l - z);
        for (size_t k = z; k < l; k++) out.push_back(a[k] ^ b[k]);
        i = l;
    }
    return out;
}

// state ^= delta (in place), the delta's records starting at `pos`
void apply_delta(std::vector<uint8_t>& state, const std::vector<uint8_t>& delta, size_t pos) {
    size_t i = 0;
    while (pos < delta.size() && i <= state.size()) {
        i += get_varint(delta, pos);
        size_t l = get_varint(delta, pos);
        for (size_t k = 0; k < l && i < state.size() && pos < delta.size(); k++) state[i++] ^= delta[pos++];
    }
}

constexpr uint8_t kDelta = 0, kWhole = 1;   // the first byte of each older state's record
}  // namespace

void RewindBuffer::clear() {
    newest_.clear();
    deltas_.clear();
    bytes_ = 0;
}

void RewindBuffer::push(const std::vector<uint8_t>& state) {
    if (!newest_.empty()) {
        // The state before this one: its difference to it, or the whole state when their sizes differ (a save
        // state's size can change within a game: an MD+ soundtrack's position is part of it as text).
        std::vector<uint8_t> rec(1, newest_.size() == state.size() ? kDelta : kWhole);
        if (rec[0] == kDelta) {
            const std::vector<uint8_t> d = encode(newest_, state);
            rec.insert(rec.end(), d.begin(), d.end());
        } else {
            rec.insert(rec.end(), newest_.begin(), newest_.end());
        }
        deltas_.push_back(std::move(rec));
        bytes_ += deltas_.back().size();
    }
    newest_ = state;
    while (!deltas_.empty() && (deltas_.size() + 1 > max_states || bytes_ + newest_.size() > max_bytes)) {
        bytes_ -= deltas_.front().size();
        deltas_.pop_front();
    }
}

bool RewindBuffer::pop(std::vector<uint8_t>& state) {
    if (newest_.empty()) return false;
    state = newest_;
    if (deltas_.empty()) {
        newest_.clear();
    } else {
        const std::vector<uint8_t>& rec = deltas_.back();
        if (rec[0] == kWhole) newest_.assign(rec.begin() + 1, rec.end());
        else apply_delta(newest_, rec, 1);
        bytes_ -= rec.size();
        deltas_.pop_back();
    }
    return true;
}
