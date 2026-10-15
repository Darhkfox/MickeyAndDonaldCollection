// Minimal PNG writer (uncompressed deflate) for screenshots.
#pragma once
#include <cstdint>
#include <string>

bool write_png(const std::wstring& path, const uint32_t* argb, int w, int h);
