// Colour look-up tables: VDP 11-bit colour codes -> 0xAARRGGBB.
#pragma once
#include <cstdint>
#include "settings.h"

// lut[(intensity << 9) | BGR333], intensity 0 normal, 1 shadow, 2 highlight.
void build_palette(uint32_t lut[2048], ColorMode mode, int brightness, int saturation, int gamma);
