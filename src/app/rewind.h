// Rewind history: save states taken every few frames, newest first out.
// The newest state is kept whole; each older one is stored as its XOR difference to the next newer state,
// with runs of zero bytes compressed away (consecutive states differ in a few KB of a ~200 KB state).
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class RewindBuffer {
public:
    void clear();
    // Appends a state; drops the oldest ones beyond the limits.
    void push(const std::vector<uint8_t>& state);
    // Removes and returns the newest state. False when empty.
    bool pop(std::vector<uint8_t>& state);
    bool empty() const { return newest_.empty(); }
    size_t count() const { return newest_.empty() ? 0 : deltas_.size() + 1; }
    size_t bytes() const { return bytes_ + newest_.size(); }

    size_t max_states = 1800;                // 60 s at one state every 2 frames (a few MB: states differ by 0.3-3 KB)
    size_t max_bytes = 64u * 1024 * 1024;

private:
    std::vector<uint8_t> newest_;
    std::deque<std::vector<uint8_t>> deltas_;   // front = oldest; back = difference between newest_ and the one before
    size_t bytes_ = 0;
};
