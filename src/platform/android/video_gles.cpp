// OpenGL ES 3.0 presentation layer for Android (see video.h); the same drawing model as the Direct3D 11 one in
// video.cpp: the game frame with its filters, then batched overlay quads (rects, rounded rects, text).
// UI glyphs are rendered by Android's own text engine (MDActivity.renderGlyphs, through JNI) into one atlas.
#include "../video.h"
#include <SDL.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numeric>
#include <vector>

namespace {

const char* kGameVS = R"(#version 300 es
uniform vec4 u_dst;   // NDC left, top, right, bottom
void main() {
    vec2 t = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(mix(u_dst.xy, u_dst.zw, t), 0.0, 1.0);
}
)";

// The pixel shaders of video.cpp, in GLSL. Pixels arrive as B,G,R,A bytes (uploaded as RGBA, so swizzled).
const char* kGameCommon = R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_tex;
uniform vec2 u_src;      // source size in texels
uniform vec2 u_scale;    // output pixels per source texel
uniform vec2 u_origin;   // destination top-left in pixels
uniform float u_alpha;
uniform float u_dim;
uniform float u_h;       // backbuffer height (GL's window y runs up)
out vec4 o_col;
vec2 sv() { return vec2(gl_FragCoord.x, u_h - gl_FragCoord.y); }
vec2 src_pos() { return (sv() - u_origin) / u_scale; }
vec4 finish(vec3 c) { return vec4(clamp(c, 0.0, 1.0) * (1.0 - u_dim), u_alpha); }
vec3 fetch(vec2 p) { return texelFetch(u_tex, clamp(ivec2(floor(p)), ivec2(0), ivec2(u_src) - 1), 0).bgr; }
vec3 sharp(vec2 p, vec2 s) {
    vec2 fl = floor(p), cd = p - fl - 0.5, rr = 0.5 - 0.5 / s;
    vec2 f = (cd - clamp(cd, -rr, rr)) * s + 0.5;
    return texture(u_tex, (fl + f) / u_src).bgr;
}
float gap_integral(float y, float gap) { return floor(y) * gap + max(fract(y) - (1.0 - gap), 0.0); }
vec3 crt_row(float y, float px) {
    float x0 = floor(px - 0.5), fx = px - 0.5 - x0;
    vec3 acc = vec3(0.0); float wsum = 0.0;
    for (int k = -1; k <= 2; k++) {
        float d = float(k) - fx, w = exp(-3.2 * d * d);
        acc += w * pow(fetch(vec2(x0 + float(k) + 0.5, y)), vec3(2.2));
        wsum += w;
    }
    return acc / wsum;
}
)";

const char* kGamePS[(int)ScaleFilter::Count] = {
    "void main() { o_col = finish(fetch(src_pos())); }\n",
    "void main() { o_col = finish(sharp(src_pos(), max(u_scale, vec2(1.0)))); }\n",
    "void main() { o_col = finish(texture(u_tex, src_pos() / u_src).bgr); }\n",
    R"(void main() {
    vec3 c = sharp(src_pos(), max(u_scale, vec2(1.0)));
    float sy = u_scale.y;
    if (sy >= 1.99) {
        float gap = max(floor(sy * 0.35 + 0.5), 1.0) / sy;
        float y0 = (sv().y - 0.5 - u_origin.y) / sy;
        float cover = (gap_integral(y0 + 1.0 / sy, gap) - gap_integral(y0, gap)) * sy;
        c *= (1.0 - 0.35 * cover) * (1.0 + 0.15 * gap);
    }
    o_col = finish(c);
}
)",
    R"(void main() {
    vec2 p = src_pos();
    float py = p.y - 0.5, ly = floor(py), fy = py - ly;
    vec3 a = crt_row(ly + 0.5, p.x), b = crt_row(ly + 1.5, p.x);
    vec3 wa = mix(vec3(0.22), vec3(0.32), sqrt(a)), wb = mix(vec3(0.22), vec3(0.32), sqrt(b));
    vec3 ea = fy / wa, eb = (1.0 - fy) / wb;
    vec3 beam = a * exp(-0.5 * ea * ea) + b * exp(-0.5 * eb * eb);
    vec3 c = mix(mix(a, b, fy), beam * 1.3, clamp(u_scale.y - 1.5, 0.0, 1.0));
    uint m = uint(sv().x) % 3u;
    c = clamp(c, 0.0, 1.0) * vec3(m == 0u ? 1.0 : 0.8, m == 1u ? 1.0 : 0.8, m == 2u ? 1.0 : 0.8);
    o_col = finish(pow(c, vec3(1.0 / 2.2)));
}
)",
};

const char* kOverlayVS = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_prm;
layout(location = 4) in float a_mode;
uniform vec2 u_screen;   // 2/W, 2/H
out vec2 v_uv;
out vec4 v_col;
flat out vec4 v_prm;
flat out float v_mode;
void main() {
    gl_Position = vec4(a_pos.x * u_screen.x - 1.0, 1.0 - a_pos.y * u_screen.y, 0.0, 1.0);
    v_uv = a_uv; v_col = a_col; v_prm = a_prm; v_mode = a_mode;
}
)";

const char* kOverlayPS = R"(#version 300 es
precision highp float;
uniform sampler2D u_atlas;
in vec2 v_uv;
in vec4 v_col;
flat in vec4 v_prm;
flat in float v_mode;
out vec4 o_col;
void main() {
    float a = 1.0;
    if (v_mode > 1.5) {            // rounded rect SDF; uv = pixel offset from centre, prm = hw, hh, r, stroke
        vec2 q = abs(v_uv) - v_prm.xy + v_prm.z;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_prm.z;
        a = clamp(0.5 - d, 0.0, 1.0);
        if (v_prm.w > 0.0) a *= clamp(d + v_prm.w + 0.5, 0.0, 1.0);
    } else if (v_mode > 0.5) {     // glyph coverage
        a = texture(u_atlas, v_uv).r;
    }
    o_col = vec4(v_col.rgb, v_col.a * a);
}
)";

struct OVert { float x, y, u, v; uint32_t col; float p[4]; float mode; };
static_assert(sizeof(OVert) == 40, "vertex layout");
const int kMaxQuads = 16384;   // per overlay draw call (16-bit indices)

struct Glyph { float adv; int ox, oy, w, h; float u0, v0, u1, v1; };
struct FontData {
    int ascent = 0, line_h = 0;
    int16_t lut[256];
    std::vector<std::pair<uint32_t, int>> ext;
    std::vector<Glyph> glyphs;
    int missing = -1;
    const Glyph* get(uint32_t cp) const {
        int i = -1;
        if (cp < 256) i = lut[cp];
        else for (auto& e : ext) if (e.first == cp) { i = e.second; break; }
        if (i < 0) i = missing;
        return i >= 0 ? &glyphs[i] : nullptr;
    }
};
const uint32_t kExtraCps[] = { 0x2190, 0x2191, 0x2192, 0x2193, 0x25B6, 0x25C0, 0x2713, 0x2022, 0x266A,
                               0x2026, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x20AC };

uint32_t to_rgba(uint32_t argb) { return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16); }

uint32_t next_cp(const char*& p, const char* end) {
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    int n = c >= 0xF8 ? -1 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (n < 0) return 0xFFFD;
    uint32_t cp = c & (0x3Fu >> n);
    for (int i = 0; i < n; i++) {
        if (p >= end || (*p & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (*p++ & 0x3Fu);
    }
    return cp;
}

void log_error(const char* msg) { __android_log_print(ANDROID_LOG_ERROR, "MickeyDonald", "%s", msg); }

GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        log_error(buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const std::string& vs, const std::string& fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        log_error(buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace

struct Video::Impl {
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    int w = 0, h = 0;
    bool vsync = true, in_frame = false, fonts_dirty = true;

    GLuint prog_game[(int)ScaleFilter::Count] = {}, prog_ov = 0;
    struct GameLoc { GLint dst, src, scale, origin, alpha, dim, h, tex; } gl_game[(int)ScaleFilter::Count] = {};
    GLint ov_screen = -1, ov_atlas = -1;
    GLuint vao_game = 0, vao_ov = 0, vb = 0, ib = 0;

    struct Slot { GLuint tex = 0; int w = 0, h = 0, tw = 0, th = 0; } slots[4];

    GLuint atlas = 0;
    std::vector<uint8_t> atlas_cpu;
    int atlas_w = 0, atlas_h = 0;
    FontData fonts[(int)FontId::Count];

    std::vector<OVert> verts;

    bool create_pipeline() {
        for (int i = 0; i < (int)ScaleFilter::Count; i++) {
            prog_game[i] = link(kGameVS, std::string(kGameCommon) + kGamePS[i]);
            if (!prog_game[i]) return false;
            GameLoc& L = gl_game[i];
            GLuint p = prog_game[i];
            L.dst = glGetUniformLocation(p, "u_dst"); L.src = glGetUniformLocation(p, "u_src");
            L.scale = glGetUniformLocation(p, "u_scale"); L.origin = glGetUniformLocation(p, "u_origin");
            L.alpha = glGetUniformLocation(p, "u_alpha"); L.dim = glGetUniformLocation(p, "u_dim");
            L.h = glGetUniformLocation(p, "u_h"); L.tex = glGetUniformLocation(p, "u_tex");
        }
        prog_ov = link(kOverlayVS, kOverlayPS);
        if (!prog_ov) return false;
        ov_screen = glGetUniformLocation(prog_ov, "u_screen");
        ov_atlas = glGetUniformLocation(prog_ov, "u_atlas");

        glGenVertexArrays(1, &vao_game);
        glGenVertexArrays(1, &vao_ov);
        glBindVertexArray(vao_ov);
        glGenBuffers(1, &vb);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)kMaxQuads * 4 * sizeof(OVert), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OVert), (void*)offsetof(OVert, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(OVert), (void*)offsetof(OVert, u));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OVert), (void*)offsetof(OVert, col));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(OVert), (void*)offsetof(OVert, p));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(OVert), (void*)offsetof(OVert, mode));
        std::vector<uint16_t> idx(kMaxQuads * 6);
        for (int q = 0; q < kMaxQuads; q++) {
            uint16_t b = (uint16_t)(q * 4);
            uint16_t* o = &idx[q * 6];
            o[0] = b; o[1] = (uint16_t)(b + 1); o[2] = (uint16_t)(b + 2);
            o[3] = (uint16_t)(b + 2); o[4] = (uint16_t)(b + 1); o[5] = (uint16_t)(b + 3);
        }
        glGenBuffers(1, &ib);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * 2), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
        return true;
    }

    void release_objects() {
        for (auto& s : slots) { if (s.tex) glDeleteTextures(1, &s.tex); s = Slot(); }
        if (atlas) { glDeleteTextures(1, &atlas); atlas = 0; }
        for (auto& p : prog_game) if (p) { glDeleteProgram(p); p = 0; }
        if (prog_ov) { glDeleteProgram(prog_ov); prog_ov = 0; }
        if (vb) { glDeleteBuffers(1, &vb); vb = 0; }
        if (ib) { glDeleteBuffers(1, &ib); ib = 0; }
        if (vao_game) { glDeleteVertexArrays(1, &vao_game); vao_game = 0; }
        if (vao_ov) { glDeleteVertexArrays(1, &vao_ov); vao_ov = 0; }
    }

    // ---- game textures ---------------------------------------------------------
    void upload(int slot, const uint32_t* px, int fw, int fh, int pitch) {
        if (slot < 0 || slot > 3 || !px || fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096 || !gl) return;
        Slot& s = slots[slot];
        s.w = fw; s.h = fh;
        if (!s.tex) {
            glGenTextures(1, &s.tex);
            glBindTexture(GL_TEXTURE_2D, s.tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, s.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
        if (s.tw != fw || s.th != fh) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            s.tw = fw; s.th = fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    // ---- fonts -----------------------------------------------------------------
    int font_base() const { return std::min(h, w * 3 / 4); }

    // Asks MDActivity.renderGlyphs for one font's glyphs; returns false if Java isn't reachable.
    bool render_font(const std::vector<uint32_t>& cps, float px, bool bold, std::vector<int>& meta, std::vector<uint8_t>& bits) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        jobject act = (jobject)SDL_AndroidGetActivity();
        if (!env || !act) return false;
        jclass cls = env->GetObjectClass(act);
        jmethodID m = env->GetStaticMethodID(cls, "renderGlyphs", "([IFZ[I)[B");
        bool ok = false;
        if (m) {
            jintArray jcps = env->NewIntArray((jsize)cps.size());
            std::vector<jint> tmp(cps.begin(), cps.end());
            env->SetIntArrayRegion(jcps, 0, (jsize)tmp.size(), tmp.data());
            jintArray jmeta = env->NewIntArray((jsize)(2 + cps.size() * 5));
            jbyteArray jbits = (jbyteArray)env->CallStaticObjectMethod(cls, m, jcps, (jfloat)px, (jboolean)bold, jmeta);
            if (!env->ExceptionCheck() && jbits) {
                meta.resize(2 + cps.size() * 5);
                env->GetIntArrayRegion(jmeta, 0, (jsize)meta.size(), (jint*)meta.data());
                bits.resize((size_t)env->GetArrayLength(jbits));
                env->GetByteArrayRegion(jbits, 0, (jsize)bits.size(), (jbyte*)bits.data());
                ok = true;
            }
            env->ExceptionClear();
            if (jbits) env->DeleteLocalRef(jbits);
            env->DeleteLocalRef(jmeta);
            env->DeleteLocalRef(jcps);
        }
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(act);
        return ok;
    }

    void build_fonts() {
        fonts_dirty = false;
        if (h <= 0 || !gl) return;
        static const float kDiv[(int)FontId::Count] = { 40.0f, 28.0f, 16.0f, 9.0f };
        static const bool kBold[(int)FontId::Count] = { false, false, true, true };
        std::vector<uint32_t> cps;
        for (uint32_t c = 0x20; c < 0x7F; c++) cps.push_back(c);
        for (uint32_t c = 0xA1; c < 0x100; c++) if (c != 0xAD) cps.push_back(c);
        for (uint32_t c : kExtraCps) cps.push_back(c);

        struct Bmp { int font, glyph, w, h, ax, ay; const uint8_t* px; };
        std::vector<Bmp> bmps;
        std::vector<uint8_t> bits[(int)FontId::Count];
        for (int f = 0; f < (int)FontId::Count; f++) {
            FontData& fd = fonts[f];
            fd = FontData();
            memset(fd.lut, 0xFF, sizeof(fd.lut));
            float px = std::max(8.0f, std::round((float)font_base() / kDiv[f]));
            std::vector<int> meta;
            if (!render_font(cps, px, kBold[f], meta, bits[f])) { log_error("video: glyphs unavailable"); continue; }
            fd.ascent = meta[0];
            fd.line_h = meta[1];
            size_t off = 0;
            for (size_t i = 0; i < cps.size(); i++) {
                const int* m = &meta[2 + i * 5];
                if (m[0] < 0) continue;
                Glyph g = {};
                g.adv = m[0] / 64.0f;
                g.ox = m[1]; g.oy = m[2]; g.w = m[3]; g.h = m[4];
                int idx = (int)fd.glyphs.size();
                if (g.w > 0 && off + (size_t)g.w * g.h <= bits[f].size()) {
                    bmps.push_back({ f, idx, g.w, g.h, 0, 0, &bits[f][off] });
                    off += (size_t)g.w * g.h;
                } else {
                    g.w = g.h = 0;
                }
                fd.glyphs.push_back(g);
                if (cps[i] < 256) fd.lut[cps[i]] = (int16_t)idx; else fd.ext.push_back({ cps[i], idx });
                if (cps[i] == '?') fd.missing = idx;
            }
        }

        // Shelf-pack (tallest first) into one R8 atlas shared by all fonts; bitmaps carry their own margin.
        std::vector<int> order(bmps.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return bmps[a].h > bmps[b].h; });
        long long area = 0;
        for (auto& b : bmps) area += (long long)(b.w + 1) * (b.h + 1);
        GLint max_dim = 4096;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_dim);
        max_dim = std::min(max_dim, 8192);
        int aw = 256;
        while ((long long)aw * aw < area * 5 / 4 && aw < max_dim) aw *= 2;
        int ah = 0;
        for (;;) {
            int x = 0, y = 0, row = 0;
            for (int i : order) {
                Bmp& b = bmps[i];
                if (x + b.w + 1 > aw) { x = 0; y += row; row = 0; }
                b.ax = x; b.ay = y;
                x += b.w + 1; row = std::max(row, b.h + 1);
            }
            ah = (y + row + 3) & ~3;
            if (ah <= max_dim || aw >= max_dim) break;
            aw *= 2;
        }
        ah = std::max(4, std::min(ah, (int)max_dim));
        atlas_w = aw; atlas_h = ah;
        atlas_cpu.assign((size_t)aw * ah, 0);
        for (auto& b : bmps) {
            if (b.ay + b.h > ah) continue;
            for (int y = 0; y < b.h; y++) memcpy(&atlas_cpu[(size_t)(b.ay + y) * aw + b.ax], b.px + (size_t)y * b.w, (size_t)b.w);
            Glyph& g = fonts[b.font].glyphs[b.glyph];
            g.u0 = (float)b.ax / aw; g.v0 = (float)b.ay / ah;
            g.u1 = (float)(b.ax + b.w) / aw; g.v1 = (float)(b.ay + b.h) / ah;
        }
        if (!atlas) {
            glGenTextures(1, &atlas);
            glBindTexture(GL_TEXTURE_2D, atlas);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, atlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE, atlas_cpu.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    void ensure_fonts() { if (fonts_dirty && gl) build_fonts(); }

    float line_width(const FontData& fd, const char* p, const char* end, float scale) const {
        float wsum = 0;
        while (p < end) {
            uint32_t cp = next_cp(p, end);
            if (cp < 0x20) continue;
            if (const Glyph* g = fd.get(cp)) wsum += g->adv;
        }
        return wsum * scale;
    }

    // ---- overlay batching -----------------------------------------------------
    void flush() {
        if (verts.empty()) return;
        if (!prog_ov) { verts.clear(); return; }
        glUseProgram(prog_ov);
        glUniform2f(ov_screen, 2.0f / (float)w, 2.0f / (float)h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glUniform1i(ov_atlas, 0);
        glBindVertexArray(vao_ov);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)kMaxQuads * 4 * sizeof(OVert), nullptr, GL_STREAM_DRAW);   // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(verts.size() * sizeof(OVert)), verts.data());
        glDrawElements(GL_TRIANGLES, (GLsizei)(verts.size() / 4 * 6), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
        verts.clear();
    }

    void quad(float x0, float y0, float x1, float y1, uint32_t ctop, uint32_t cbot,
              float u0, float v0, float u1, float v1, const float* prm, float mode) {
        if (!in_frame) return;
        if (verts.size() >= (size_t)kMaxQuads * 4) flush();
        float p[4] = { 0, 0, 0, 0 };
        if (prm) memcpy(p, prm, sizeof(p));
        OVert v[4] = {
            { x0, y0, u0, v0, ctop, { p[0], p[1], p[2], p[3] }, mode },
            { x1, y0, u1, v0, ctop, { p[0], p[1], p[2], p[3] }, mode },
            { x0, y1, u0, v1, cbot, { p[0], p[1], p[2], p[3] }, mode },
            { x1, y1, u1, v1, cbot, { p[0], p[1], p[2], p[3] }, mode },
        };
        verts.insert(verts.end(), v, v + 4);
    }

    void round_rect(Rect r, float radius, float stroke, uint32_t color) {
        if (r.w <= 0 || r.h <= 0) return;
        float hw = r.w * 0.5f, hh = r.h * 0.5f;
        float prm[4] = { hw, hh, std::max(0.0f, std::min(radius, std::min(hw, hh))), stroke };
        float cx = r.x + hw, cy = r.y + hh;
        float x0 = r.x - 1, y0 = r.y - 1, x1 = r.x + r.w + 1, y1 = r.y + r.h + 1;
        uint32_t c = to_rgba(color);
        quad(x0, y0, x1, y1, c, c, x0 - cx, y0 - cy, x1 - cx, y1 - cy, prm, 2.0f);
    }
};

// ---- public API ---------------------------------------------------------------------

Video::Video() : impl(new Impl) {}
Video::~Video() { shutdown(); delete impl; }

bool Video::init(NativeWindow window, int width, int height) {
    shutdown();
    Impl& I = *impl;
    I.window = window;
    I.gl = SDL_GL_CreateContext(window);
    if (!I.gl) { log_error(SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(window, I.gl);
    SDL_GL_SetSwapInterval(I.vsync ? 1 : 0);
    I.w = std::max(1, width); I.h = std::max(1, height);
    if (!I.create_pipeline()) { log_error("video: shaders failed"); return false; }
    I.fonts_dirty = true;
    I.ensure_fonts();
    return true;
}

void Video::shutdown() {
    Impl& I = *impl;
    if (I.gl) {
        I.release_objects();
        SDL_GL_DeleteContext(I.gl);
        I.gl = nullptr;
    }
    I.atlas_cpu.clear();
    for (auto& f : I.fonts) f = FontData();
    I.verts.clear();
    I.in_frame = false;
    I.window = nullptr;
}

void Video::resize(int width, int height) {
    Impl& I = *impl;
    if (width <= 0 || height <= 0 || (width == I.w && height == I.h)) return;
    const int old_base = I.font_base();
    I.flush();
    I.w = width; I.h = height;
    if (I.font_base() != old_base) I.fonts_dirty = true;
}

int  Video::width() const { return impl->w; }
int  Video::height() const { return impl->h; }
void Video::set_vsync(bool on) {
    if (impl->vsync == on) return;
    impl->vsync = on;
    if (impl->gl) SDL_GL_SetSwapInterval(on ? 1 : 0);
}

void Video::upload_frame(const uint32_t* pixels, int w, int h, int pitch) { impl->upload(0, pixels, w, h, pitch); }
void Video::upload_frame_slot(int slot, const uint32_t* pixels, int w, int h, int pitch) { impl->upload(slot, pixels, w, h, pitch); }

void Video::begin_frame(uint32_t clear_color) {
    Impl& I = *impl;
    I.in_frame = false;
    I.verts.clear();
    if (!I.gl) return;
    I.ensure_fonts();
    glViewport(0, 0, I.w, I.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(((clear_color >> 16) & 255) / 255.0f, ((clear_color >> 8) & 255) / 255.0f, (clear_color & 255) / 255.0f,
                 (clear_color >> 24) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    I.in_frame = true;
}

void Video::draw_game(int slot, Rect dst, ScaleFilter filter, float alpha, float dim) {
    Impl& I = *impl;
    if (!I.in_frame || slot < 0 || slot > 3 || dst.w <= 0 || dst.h <= 0) return;
    Impl::Slot& s = I.slots[slot];
    int f = (int)filter;
    if (!s.tex || f < 0 || f >= (int)ScaleFilter::Count || alpha <= 0 || !I.prog_game[f]) return;
    I.flush();
    const Impl::GameLoc& L = I.gl_game[f];
    glUseProgram(I.prog_game[f]);
    glUniform4f(L.dst, dst.x / I.w * 2 - 1, 1 - dst.y / I.h * 2, (dst.x + dst.w) / I.w * 2 - 1, 1 - (dst.y + dst.h) / I.h * 2);
    glUniform2f(L.src, (float)s.w, (float)s.h);
    glUniform2f(L.scale, dst.w / s.w, dst.h / s.h);
    glUniform2f(L.origin, dst.x, dst.y);
    glUniform1f(L.alpha, std::min(alpha, 1.0f));
    glUniform1f(L.dim, std::max(0.0f, std::min(dim, 1.0f)));
    glUniform1f(L.h, (float)I.h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glUniform1i(L.tex, 0);
    glBindVertexArray(I.vao_game);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void Video::fill_rect(Rect r, uint32_t color) { fill_rect_gradient(r, color, color); }

void Video::fill_rect_gradient(Rect r, uint32_t top, uint32_t bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    impl->quad(r.x, r.y, r.x + r.w, r.y + r.h, to_rgba(top), to_rgba(bottom), 0, 0, 0, 0, nullptr, 0.0f);
}

void Video::fill_round_rect(Rect r, float radius, uint32_t color) { impl->round_rect(r, radius, 0.0f, color); }

void Video::outline_round_rect(Rect r, float radius, float thickness, uint32_t color) {
    if (thickness > 0) impl->round_rect(r, radius, thickness, color);
}

void Video::draw_text(FontId font, float x, float y, const std::string& utf8, uint32_t color, Align align, float scale) {
    Impl& I = *impl;
    int fi = (int)font;
    if (!I.in_frame || fi < 0 || fi >= (int)FontId::Count || !I.atlas || scale <= 0) return;
    const FontData& fd = I.fonts[fi];
    if (fd.glyphs.empty()) return;
    uint32_t col = to_rgba(color);
    bool snap = std::fabs(scale - 1.0f) < 1e-3f;
    const char* p = utf8.data(), *end = p + utf8.size();
    float line_y = y;
    for (;;) {
        const char* le = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!le) le = end;
        float lw = I.line_width(fd, p, le, scale);
        float pen = x - (align == Align::Center ? lw * 0.5f : align == Align::Right ? lw : 0.0f);
        float base = line_y + fd.ascent * scale;
        if (snap) base = std::floor(base + 0.5f);
        for (const char* q = p; q < le;) {
            uint32_t cp = next_cp(q, le);
            if (cp < 0x20) continue;
            const Glyph* g = fd.get(cp);
            if (!g) continue;
            if (g->w > 0) {
                float gx = snap ? std::floor(pen + 0.5f) + g->ox : pen + g->ox * scale;
                float gy = snap ? base + g->oy : base + g->oy * scale;
                I.quad(gx, gy, gx + g->w * scale, gy + g->h * scale, col, col, g->u0, g->v0, g->u1, g->v1, nullptr, 1.0f);
            }
            pen += g->adv * scale;
        }
        if (le >= end) break;
        p = le + 1;
        line_y += fd.line_h * scale;
    }
}

float Video::measure_text(FontId font, const std::string& utf8, float scale) {
    Impl& I = *impl;
    int fi = (int)font;
    if (fi < 0 || fi >= (int)FontId::Count) return 0;
    I.ensure_fonts();
    float best = 0;
    const char* p = utf8.data(), *end = p + utf8.size();
    for (;;) {
        const char* le = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!le) le = end;
        best = std::max(best, I.line_width(I.fonts[fi], p, le, scale));
        if (le >= end) break;
        p = le + 1;
    }
    return best;
}

float Video::line_height(FontId font, float scale) {
    int fi = (int)font;
    if (fi < 0 || fi >= (int)FontId::Count) return 0;
    impl->ensure_fonts();
    return impl->fonts[fi].line_h * scale;
}

void Video::rebuild_fonts() { impl->fonts_dirty = true; impl->ensure_fonts(); }

bool Video::capture(std::vector<uint32_t>& pixels, int& w, int& h) {
    Impl& I = *impl;
    if (!I.gl) return false;
    I.flush();
    w = I.w; h = I.h;
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    pixels.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uint8_t* s = &rgba[(size_t)(h - 1 - y) * w * 4];
        for (int x = 0; x < w; x++, s += 4)
            pixels[(size_t)y * w + x] = 0xFF000000u | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
    }
    return true;
}

void Video::end_frame() {
    Impl& I = *impl;
    if (!I.in_frame) return;
    I.flush();
    I.in_frame = false;
    SDL_GL_SwapWindow(I.window);
}
