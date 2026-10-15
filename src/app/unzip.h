// Minimal .zip reading for zipped ROMs: stored and deflated entries (no ZIP64, no encryption).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ZipEntry {
    std::string name;
    uint32_t crc = 0, comp_size = 0, size = 0, local_offset = 0;
    uint16_t method = 0, flags = 0;
};

// Reads the archive's central directory. False if it is not a zip this reader handles.
bool zip_list(const std::wstring& path, std::vector<ZipEntry>& out);
// Extracts one entry and checks its CRC32.
bool zip_read(const std::wstring& path, const ZipEntry& e, std::vector<uint8_t>& out);
// Raw deflate (RFC 1951) decoder; `expected` is the output size, which must come out exactly.
bool inflate_raw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expected);
