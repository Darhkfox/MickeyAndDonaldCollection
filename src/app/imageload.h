// Picture files the player adds (the launcher's covers and background): PNG or JPEG, through stb_image
// (third_party/stb, public domain).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The picture as 0xAARRGGBB pixels, row by row, shrunk to `max_side` pixels on its longer side if it's bigger
// (areas averaged). False if the file can't be read or isn't a PNG or JPEG.
bool load_image_file(const std::wstring& path, int max_side, std::vector<uint32_t>& argb, int* w, int* h);
