// PNG and JPEG pictures: see imageload.h.
#include "imageload.h"
#include <algorithm>
#include "sys.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO              // (read here: the paths are wide)
#define STBI_NO_FAILURE_STRINGS
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

bool load_image_file(const std::wstring& path, int max_side, std::vector<uint32_t>& argb, int* w, int* h) {
    std::vector<uint8_t> file;
    if (!read_file(path, file) || file.empty() || file.size() > (64u << 20)) return false;
    int sw = 0, sh = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(file.data(), (int)file.size(), &sw, &sh, &n, 4);
    if (!px) return false;
    if (sw <= 0 || sh <= 0) {
        stbi_image_free(px);
        return false;
    }
    // (too big: shrunk, each pixel the average of the area it stands for)
    const int big = std::max(sw, sh);
    const int dw = big > max_side ? std::max(1, (int)((int64_t)sw * max_side / big)) : sw;
    const int dh = big > max_side ? std::max(1, (int)((int64_t)sh * max_side / big)) : sh;
    argb.assign((size_t)dw * dh, 0);
    for (int y = 0; y < dh; y++) {
        const int y0 = (int)((int64_t)y * sh / dh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * sh / dh));
        for (int x = 0; x < dw; x++) {
            const int x0 = (int)((int64_t)x * sw / dw), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * sw / dw));
            uint32_t s[4] = {}, count = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const stbi_uc* p = px + ((size_t)yy * sw + xx) * 4;
                    for (int c = 0; c < 4; c++) s[c] += p[c];
                    count++;
                }
            const uint32_t r = s[0] / count, g = s[1] / count, b = s[2] / count, a = s[3] / count;
            argb[(size_t)y * dw + x] = a << 24 | r << 16 | g << 8 | b;
        }
    }
    stbi_image_free(px);
    *w = dw;
    *h = dh;
    return true;
}
