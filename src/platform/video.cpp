// Direct3D 11 presentation layer (see video.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <dwrite_2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>
#include "video.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwrite.lib")

namespace {

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// All shaders live in one source; entry points are compiled separately.
const char kHlsl[] = R"HLSL(
cbuffer GameCB : register(b0) {
    float4 g_dst;      // NDC left, top, right, bottom
    float2 g_src;      // source size in texels
    float2 g_scale;    // output pixels per source texel
    float2 g_origin;   // destination top-left in pixels
    float  g_alpha;
    float  g_dim;
};
cbuffer OverlayCB : register(b1) { float4 g_screen; };   // 2/W, 2/H
Texture2D    g_tex    : register(t0);
SamplerState g_point  : register(s0);
SamplerState g_linear : register(s1);

float4 vs_game(uint id : SV_VertexID) : SV_Position {
    float2 t = float2(id & 1, id >> 1);
    return float4(lerp(g_dst.xy, g_dst.zw, t), 0, 1);
}
float2 src_pos(float4 sv) { return (sv.xy - g_origin) / g_scale; }   // texel space, centres at .5
float4 finish(float3 c) { return float4(saturate(c) * (1 - g_dim), g_alpha); }
float3 fetch(float2 p) { return g_tex.Load(int3(clamp(int2(floor(p)), 0, int2(g_src) - 1), 0)).rgb; }
// Sharp bilinear: flat texel interiors, a one-output-pixel wide linear blend at texel edges.
float3 sharp(float2 p, float2 s) {
    float2 fl = floor(p), cd = p - fl - 0.5, rr = 0.5 - 0.5 / s;
    float2 f = (cd - clamp(cd, -rr, rr)) * s + 0.5;
    return g_tex.SampleLevel(g_linear, (fl + f) / g_src, 0).rgb;
}
float4 ps_nearest(float4 sv : SV_Position) : SV_Target { return finish(fetch(src_pos(sv))); }
float4 ps_sharp(float4 sv : SV_Position) : SV_Target { return finish(sharp(src_pos(sv), max(g_scale, 1))); }
float4 ps_bilinear(float4 sv : SV_Position) : SV_Target {
    return finish(g_tex.SampleLevel(g_linear, src_pos(sv) / g_src, 0).rgb);
}

// Integral of the scanline-gap indicator (the gap is the last `gap` fraction of each source line);
// differencing it over the output pixel's footprint gives an alias-free coverage at any scale.
float gap_integral(float y, float gap) { return floor(y) * gap + max(frac(y) - (1 - gap), 0); }
float4 ps_scanlines(float4 sv : SV_Position) : SV_Target {
    float3 c = sharp(src_pos(sv), max(g_scale, 1));
    float sy = g_scale.y;
    if (sy >= 1.99) {
        float gap = max(round(sy * 0.35), 1) / sy;     // ~1/3 of a line, whole output rows
        float y0 = (sv.y - 0.5 - g_origin.y) / sy;
        float cover = (gap_integral(y0 + 1 / sy, gap) - gap_integral(y0, gap)) * sy;
        c *= (1 - 0.35 * cover) * (1 + 0.15 * gap);
    }
    return finish(c);
}

float3 crt_row(float y, float px) {       // horizontal gaussian over 4 taps, in linear light
    float x0 = floor(px - 0.5), fx = px - 0.5 - x0;
    float3 acc = 0; float wsum = 0;
    [unroll] for (int k = -1; k <= 2; k++) {
        float d = k - fx, w = exp(-3.2 * d * d);
        acc += w * pow(fetch(float2(x0 + k + 0.5, y)), 2.2);
        wsum += w;
    }
    return acc / wsum;
}
float4 ps_crt(float4 sv : SV_Position) : SV_Target {
    float2 p = src_pos(sv);
    float py = p.y - 0.5, ly = floor(py), fy = py - ly;
    float3 a = crt_row(ly + 0.5, p.x), b = crt_row(ly + 1.5, p.x);
    float3 wa = lerp(0.22, 0.32, sqrt(a)), wb = lerp(0.22, 0.32, sqrt(b));   // bright beams bloom wider
    float3 ea = fy / wa, eb = (1 - fy) / wb;
    float3 beam = a * exp(-0.5 * ea * ea) + b * exp(-0.5 * eb * eb);
    float3 c = lerp(lerp(a, b, fy), beam * 1.3, saturate(g_scale.y - 1.5));   // no beams when tiny
    uint m = uint(sv.x) % 3;                                                   // aperture grille
    c = saturate(c) * float3(m == 0 ? 1.0 : 0.8, m == 1 ? 1.0 : 0.8, m == 2 ? 1.0 : 0.8);
    return finish(pow(c, 1 / 2.2));
}

struct OvIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0;
               float4 prm : TEXCOORD1; float mode : TEXCOORD2; };
struct OvOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0;
               nointerpolation float4 prm : TEXCOORD1; nointerpolation float mode : TEXCOORD2; };
OvOut vs_overlay(OvIn i) {
    OvOut o;
    o.pos = float4(i.pos.x * g_screen.x - 1, 1 - i.pos.y * g_screen.y, 0, 1);
    o.uv = i.uv; o.col = i.col; o.prm = i.prm; o.mode = i.mode;
    return o;
}
float4 ps_overlay(OvOut i) : SV_Target {
    float a = 1;
    if (i.mode > 1.5) {            // rounded rect SDF; uv = pixel offset from centre, prm = hw, hh, r, stroke
        float2 q = abs(i.uv) - i.prm.xy + i.prm.z;
        float d = length(max(q, 0)) + min(max(q.x, q.y), 0) - i.prm.z;
        a = saturate(0.5 - d);
        if (i.prm.w > 0) a *= saturate(d + i.prm.w + 0.5);
    } else if (i.mode > 0.5) {     // glyph coverage
        a = g_tex.Sample(g_linear, i.uv).r;
    }
    return float4(i.col.rgb, i.col.a * a);
}
)HLSL";

const char* const kGamePS[(int)ScaleFilter::Count] = { "ps_nearest", "ps_sharp", "ps_bilinear", "ps_scanlines", "ps_crt" };

struct OVert { float x, y, u, v; uint32_t col; float p[4]; float mode; };
static_assert(sizeof(OVert) == 40, "vertex layout");
struct GameCB { float dst[4]; float src[2]; float scale[2]; float origin[2]; float alpha, dim; };
const int kMaxQuads = 16384;   // per overlay draw call (16-bit indices)

struct Glyph { float adv; int ox, oy, w, h; float u0, v0, u1, v1; };   // ox/oy: quad offset from pen/baseline
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
// Code points beyond Latin-1 that the atlas carries.
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

void log_error(const char* msg) { OutputDebugStringA(msg); fputs(msg, stderr); }

} // namespace

struct Video::Impl {
    HWND hwnd = nullptr;
    int w = 0, h = 0;
    bool vsync = true, tearing = false, in_frame = false, fonts_dirty = true;
    UINT sc_flags = 0;
    ULONGLONG next_recover = 0;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3DBlob* blob_vs_game = nullptr, *blob_vs_ov = nullptr, *blob_ps_ov = nullptr;
    ID3DBlob* blob_ps_game[(int)ScaleFilter::Count] = {};
    ID3D11VertexShader* vs_game = nullptr, *vs_ov = nullptr;
    ID3D11PixelShader* ps_game[(int)ScaleFilter::Count] = {}, *ps_ov = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cb_game = nullptr, *cb_ov = nullptr, *vb = nullptr, *ib = nullptr;
    ID3D11SamplerState* samp[2] = {};
    ID3D11BlendState* blend = nullptr;
    ID3D11RasterizerState* rast = nullptr;

    struct Slot { ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; int w = 0, h = 0, tw = 0, th = 0;
                  std::vector<uint32_t> cpu; } slots[4];

    ID3D11Texture2D* atlas = nullptr;
    ID3D11ShaderResourceView* atlas_srv = nullptr;
    std::vector<uint8_t> atlas_cpu;
    int atlas_w = 0, atlas_h = 0;
    FontData fonts[(int)FontId::Count];
    IDWriteFactory2* dw = nullptr;
    IDWriteFontFace* faces[(int)FontId::Count][4] = {};   // primary face + fallbacks

    std::vector<OVert> verts;

    // ---- setup ---------------------------------------------------------------
    ID3DBlob* compile(const char* entry, const char* target) {
        ID3DBlob* code = nullptr, *err = nullptr;
        HRESULT hr = D3DCompile(kHlsl, sizeof(kHlsl) - 1, "video.hlsl", nullptr, nullptr, entry, target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
        if (FAILED(hr)) {
            log_error("video: shader compile failed: ");
            log_error(err ? (const char*)err->GetBufferPointer() : entry);
            log_error("\n");
        }
        release(err);
        return code;
    }
    bool compile_all() {
        if (!blob_vs_game) blob_vs_game = compile("vs_game", "vs_4_0");
        if (!blob_vs_ov) blob_vs_ov = compile("vs_overlay", "vs_4_0");
        if (!blob_ps_ov) blob_ps_ov = compile("ps_overlay", "ps_4_0");
        bool ok = blob_vs_game && blob_vs_ov && blob_ps_ov;
        for (int i = 0; i < (int)ScaleFilter::Count; i++) {
            if (!blob_ps_game[i]) blob_ps_game[i] = compile(kGamePS[i], "ps_4_0");
            ok = ok && blob_ps_game[i];
        }
        return ok;
    }

    bool create_device() {
        const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                          D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = E_FAIL;
        for (D3D_DRIVER_TYPE type : { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP }) {
            hr = D3D11CreateDevice(nullptr, type, nullptr, flags, fls, 4, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
            if (hr == E_INVALIDARG)   // pre-11.1 runtime
                hr = D3D11CreateDevice(nullptr, type, nullptr, flags, fls + 1, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
            if (SUCCEEDED(hr)) break;
        }
        return SUCCEEDED(hr);
    }

    bool create_swapchain() {
        IDXGIDevice1* dd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
        bool ok = SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dd)) &&
                  SUCCEEDED(dd->GetAdapter(&ad)) &&
                  SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2));
        if (ok) {
            dd->SetMaximumFrameLatency(1);
            tearing = false;
            IDXGIFactory5* f5 = nullptr;
            if (SUCCEEDED(f2->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5))) {
                BOOL allow = FALSE;
                if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
                    tearing = allow != FALSE;
                f5->Release();
            }
            DXGI_SWAP_CHAIN_DESC1 d = {};
            d.Width = (UINT)w; d.Height = (UINT)h;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d.Scaling = DXGI_SCALING_STRETCH;
            d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            // Flip-discard (Win10) -> flip-sequential (Win8) -> legacy blit model.
            const DXGI_SWAP_EFFECT effects[] = { DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD };
            ok = false;
            for (DXGI_SWAP_EFFECT e : effects) {
                bool flip = e != DXGI_SWAP_EFFECT_DISCARD;
                d.SwapEffect = e;
                d.BufferCount = flip ? 3 : 1;
                d.AlphaMode = flip ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_UNSPECIFIED;
                d.Flags = (flip && tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
                if (SUCCEEDED(f2->CreateSwapChainForHwnd(dev, hwnd, &d, nullptr, nullptr, &sc))) { ok = true; break; }
            }
            sc_flags = d.Flags;
            tearing = ok && (d.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
            f2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        }
        release(f2); release(ad); release(dd);
        return ok;
    }

    bool create_rtv() {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
        HRESULT hr = dev->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();
        return SUCCEEDED(hr);
    }

    void update_screen_cb() {
        if (!ctx || !cb_ov || w <= 0 || h <= 0) return;
        float c[4] = { 2.0f / (float)w, 2.0f / (float)h, 0, 0 };
        ctx->UpdateSubresource(cb_ov, 0, nullptr, c, 0, 0);
    }

    bool create_pipeline() {
        if (!compile_all()) return false;
        bool ok = SUCCEEDED(dev->CreateVertexShader(blob_vs_game->GetBufferPointer(), blob_vs_game->GetBufferSize(), nullptr, &vs_game)) &&
                  SUCCEEDED(dev->CreateVertexShader(blob_vs_ov->GetBufferPointer(), blob_vs_ov->GetBufferSize(), nullptr, &vs_ov)) &&
                  SUCCEEDED(dev->CreatePixelShader(blob_ps_ov->GetBufferPointer(), blob_ps_ov->GetBufferSize(), nullptr, &ps_ov));
        for (int i = 0; ok && i < (int)ScaleFilter::Count; i++)
            ok = SUCCEEDED(dev->CreatePixelShader(blob_ps_game[i]->GetBufferPointer(), blob_ps_game[i]->GetBufferSize(), nullptr, &ps_game[i]));
        if (!ok) return false;

        const D3D11_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,          0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(dev->CreateInputLayout(il, 5, blob_vs_ov->GetBufferPointer(), blob_vs_ov->GetBufferSize(), &layout))) return false;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(GameCB); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb_game))) return false;
        bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.CPUAccessFlags = 0;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb_ov))) return false;
        bd.ByteWidth = kMaxQuads * 4 * sizeof(OVert); bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &vb))) return false;
        std::vector<uint16_t> idx(kMaxQuads * 6);
        for (int q = 0; q < kMaxQuads; q++) {
            uint16_t b = (uint16_t)(q * 4);
            uint16_t* o = &idx[q * 6];
            o[0] = b; o[1] = (uint16_t)(b + 1); o[2] = (uint16_t)(b + 2);
            o[3] = (uint16_t)(b + 2); o[4] = (uint16_t)(b + 1); o[5] = (uint16_t)(b + 3);
        }
        bd.ByteWidth = (UINT)(idx.size() * 2); bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.CPUAccessFlags = 0;
        D3D11_SUBRESOURCE_DATA sd = { idx.data(), 0, 0 };
        if (FAILED(dev->CreateBuffer(&bd, &sd, &ib))) return false;

        D3D11_SAMPLER_DESC sm = {};
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sm.MaxLOD = D3D11_FLOAT32_MAX;
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(dev->CreateSamplerState(&sm, &samp[0]))) return false;
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (FAILED(dev->CreateSamplerState(&sm, &samp[1]))) return false;

        D3D11_BLEND_DESC bl = {};
        auto& rt = bl.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bl, &blend))) return false;

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        if (FAILED(dev->CreateRasterizerState(&rd, &rast))) return false;
        update_screen_cb();
        return true;
    }

    bool create_device_objects() {
        if (!create_device() || !create_swapchain() || !create_rtv() || !create_pipeline()) {
            release_device_objects();
            return false;
        }
        return true;
    }

    void release_device_objects() {
        if (ctx) { ctx->ClearState(); ctx->Flush(); }
        for (auto& s : slots) { release(s.srv); release(s.tex); s.tw = s.th = 0; }
        release(atlas_srv); release(atlas);
        release(rast); release(blend); release(samp[0]); release(samp[1]);
        release(ib); release(vb); release(cb_ov); release(cb_game); release(layout);
        for (auto*& p : ps_game) release(p);
        release(ps_ov); release(vs_ov); release(vs_game);
        release(rtv); release(sc); release(ctx); release(dev);
    }

    // Recreate everything after device removal; CPU copies of textures are re-uploaded.
    bool recover() {
        verts.clear();
        release_device_objects();
        if (!create_device_objects()) { next_recover = GetTickCount64() + 1000; return false; }
        for (int i = 0; i < 4; i++) if (!slots[i].cpu.empty()) upload_slot(i);
        if (!atlas_cpu.empty()) create_atlas_texture();
        return true;
    }
    bool device_lost(HRESULT hr) const { return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET; }

    // ---- game textures ---------------------------------------------------------
    void upload_slot(int i) {
        Slot& s = slots[i];
        if (!dev || s.w <= 0 || s.h <= 0) return;
        if (!s.tex || s.tw != s.w || s.th != s.h) {
            release(s.srv); release(s.tex);
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = (UINT)s.w; td.Height = (UINT)s.h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &s.tex)) ||
                FAILED(dev->CreateShaderResourceView(s.tex, nullptr, &s.srv))) { release(s.tex); return; }
            s.tw = s.w; s.th = s.h;
        }
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(s.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        for (int y = 0; y < s.h; y++)
            memcpy((uint8_t*)m.pData + (size_t)y * m.RowPitch, &s.cpu[(size_t)y * s.w], (size_t)s.w * 4);
        ctx->Unmap(s.tex, 0);
    }

    void upload(int slot, const uint32_t* px, int fw, int fh, int pitch) {
        if (slot < 0 || slot > 3 || !px || fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096) return;
        Slot& s = slots[slot];
        s.w = fw; s.h = fh;
        s.cpu.resize((size_t)fw * fh);
        for (int y = 0; y < fh; y++) memcpy(&s.cpu[(size_t)y * fw], px + (size_t)y * pitch, (size_t)fw * 4);
        upload_slot(slot);
    }

    // ---- fonts -----------------------------------------------------------------
    bool init_fonts() {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), (IUnknown**)&dw))) return false;
        IDWriteFontCollection* coll = nullptr;
        if (FAILED(dw->GetSystemFontCollection(&coll, FALSE))) return false;
        auto face = [&](const wchar_t* family, DWRITE_FONT_WEIGHT wt) -> IDWriteFontFace* {
            UINT32 idx = 0; BOOL exists = FALSE;
            IDWriteFontFamily* fam = nullptr; IDWriteFont* font = nullptr; IDWriteFontFace* ff = nullptr;
            if (SUCCEEDED(coll->FindFamilyName(family, &idx, &exists)) && exists &&
                SUCCEEDED(coll->GetFontFamily(idx, &fam)) &&
                SUCCEEDED(fam->GetFirstMatchingFont(wt, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)))
                font->CreateFontFace(&ff);
            release(font); release(fam);
            return ff;
        };
        const DWRITE_FONT_WEIGHT weights[] = { DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_WEIGHT_BOLD };
        for (int f = 0; f < (int)FontId::Count; f++) {
            IDWriteFontFace** chain = faces[f];
            int n = 0;
            if (IDWriteFontFace* p = face(L"Segoe UI", weights[f])) chain[n++] = p;
            if (IDWriteFontFace* p = face(L"Segoe UI Symbol", DWRITE_FONT_WEIGHT_NORMAL)) chain[n++] = p;
            for (const wchar_t* alt : { L"Arial", L"Tahoma" })
                if (n < 3) if (IDWriteFontFace* p = face(alt, weights[f])) chain[n++] = p;
        }
        release(coll);
        return faces[0][0] != nullptr;
    }

    void create_atlas_texture() {
        release(atlas_srv); release(atlas);
        if (!dev || atlas_cpu.empty()) return;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)atlas_w; td.Height = (UINT)atlas_h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { atlas_cpu.data(), (UINT)atlas_w, 0 };
        if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &atlas)))
            dev->CreateShaderResourceView(atlas, nullptr, &atlas_srv);
    }

    // UI text is sized by the window height, or by 3/4 of the width in windows narrower than 4:3
    // (square or portrait), so the launcher and menus still fit across.
    int font_base() const { return std::min(h, w * 3 / 4); }

    void build_fonts() {
        fonts_dirty = false;
        if (!dw || h <= 0) return;
        static const float kDiv[(int)FontId::Count] = { 40.0f, 28.0f, 16.0f, 9.0f };
        std::vector<uint32_t> cps;
        for (uint32_t c = 0x20; c < 0x7F; c++) cps.push_back(c);
        for (uint32_t c = 0xA1; c < 0x100; c++) if (c != 0xAD) cps.push_back(c);
        for (uint32_t c : kExtraCps) cps.push_back(c);

        struct Bmp { int font, glyph, w, h, ax, ay; std::vector<uint8_t> px; };
        std::vector<Bmp> bmps;
        for (int f = 0; f < (int)FontId::Count; f++) {
            FontData& fd = fonts[f];
            fd = FontData();
            memset(fd.lut, 0xFF, sizeof(fd.lut));
            IDWriteFontFace* main = faces[f][0];
            if (!main) continue;
            float px = std::max(8.0f, std::round((float)font_base() / kDiv[f]));
            DWRITE_FONT_METRICS fm;
            main->GetMetrics(&fm);
            float k = px / fm.designUnitsPerEm;
            fd.ascent = (int)std::ceil(fm.ascent * k);
            fd.line_h = (int)std::ceil((fm.ascent + fm.descent + fm.lineGap) * k);
            for (uint32_t cp : cps) {
                IDWriteFontFace* face = nullptr;
                UINT16 gi = 0;
                for (int j = 0; j < 4 && !face; j++) {
                    if (!faces[f][j]) continue;
                    UINT32 c = cp;
                    if (SUCCEEDED(faces[f][j]->GetGlyphIndices(&c, 1, &gi)) && gi) face = faces[f][j];
                }
                if (!face) continue;
                DWRITE_FONT_METRICS ffm; DWRITE_GLYPH_METRICS gm;
                face->GetMetrics(&ffm);
                if (FAILED(face->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE))) continue;
                Glyph g = {};
                g.adv = gm.advanceWidth * px / ffm.designUnitsPerEm;

                Bmp b = { f, (int)fd.glyphs.size(), 0, 0, 0, 0, {} };
                DWRITE_GLYPH_RUN run = {};
                FLOAT adv0 = 0; DWRITE_GLYPH_OFFSET off0 = {};
                run.fontFace = face; run.fontEmSize = px; run.glyphCount = 1;
                run.glyphIndices = &gi; run.glyphAdvances = &adv0; run.glyphOffsets = &off0;
                IDWriteGlyphRunAnalysis* ana = nullptr;
                if (SUCCEEDED(dw->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                        DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                        0.0f, 0.0f, &ana))) {
                    RECT r;
                    if (SUCCEEDED(ana->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &r)) && r.right > r.left && r.bottom > r.top) {
                        b.w = r.right - r.left; b.h = r.bottom - r.top;
                        b.px.resize((size_t)b.w * b.h);
                        if (SUCCEEDED(ana->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &r, b.px.data(), (UINT32)b.px.size()))) {
                            g.ox = r.left - 1; g.oy = r.top - 1;   // quads carry a 1px empty margin for filtering
                            g.w = b.w + 2; g.h = b.h + 2;
                        } else b.w = b.h = 0;
                    }
                    ana->Release();
                }
                int idx = (int)fd.glyphs.size();
                fd.glyphs.push_back(g);
                if (cp < 256) fd.lut[cp] = (int16_t)idx; else fd.ext.push_back({ cp, idx });
                if (cp == '?') fd.missing = idx;
                if (b.w > 0) bmps.push_back(std::move(b));
            }
        }

        // Shelf-pack (tallest first) into one R8 atlas shared by all fonts.
        std::vector<int> order(bmps.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return bmps[a].h > bmps[b].h; });
        long long area = 0;
        for (auto& b : bmps) area += (long long)(b.w + 3) * (b.h + 3);
        const int max_dim = (dev && dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? 16384 : 8192;
        int aw = 256;
        while ((long long)aw * aw < area * 5 / 4 && aw < max_dim) aw *= 2;
        int ah = 0;
        for (;;) {
            int x = 0, y = 0, row = 0;
            for (int i : order) {
                Bmp& b = bmps[i];
                if (x + b.w + 3 > aw) { x = 0; y += row; row = 0; }
                b.ax = x; b.ay = y;           // cell = 1px margin + bitmap + 1px margin + 1px gap
                x += b.w + 3; row = std::max(row, b.h + 3);
            }
            ah = (y + row + 3) & ~3;
            if (ah <= max_dim || aw >= max_dim) break;
            aw *= 2;
        }
        ah = std::max(4, std::min(ah, max_dim));
        atlas_w = aw; atlas_h = ah;
        atlas_cpu.assign((size_t)aw * ah, 0);
        for (auto& b : bmps) {
            if (b.ay + b.h + 2 > ah) continue;
            for (int y = 0; y < b.h; y++)
                memcpy(&atlas_cpu[(size_t)(b.ay + 1 + y) * aw + b.ax + 1], &b.px[(size_t)y * b.w], (size_t)b.w);
            Glyph& g = fonts[b.font].glyphs[b.glyph];
            g.u0 = (float)b.ax / aw; g.v0 = (float)b.ay / ah;
            g.u1 = (float)(b.ax + b.w + 2) / aw; g.v1 = (float)(b.ay + b.h + 2) / ah;
        }
        create_atlas_texture();
    }

    void ensure_fonts() { if (fonts_dirty && dev) build_fonts(); }

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
        if (!ctx || !vb) { verts.clear(); return; }
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { verts.clear(); return; }
        memcpy(m.pData, verts.data(), verts.size() * sizeof(OVert));
        ctx->Unmap(vb, 0);
        UINT stride = sizeof(OVert), off = 0;
        ctx->IASetInputLayout(layout);
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
        ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs_ov, nullptr, 0);
        ctx->VSSetConstantBuffers(1, 1, &cb_ov);
        ctx->PSSetShader(ps_ov, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &atlas_srv);
        ctx->PSSetSamplers(0, 2, samp);
        ctx->DrawIndexed((UINT)(verts.size() / 4 * 6), 0, 0);
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
        float x0 = r.x - 1, y0 = r.y - 1, x1 = r.x + r.w + 1, y1 = r.y + r.h + 1;   // room for the AA fringe
        uint32_t c = to_rgba(color);
        quad(x0, y0, x1, y1, c, c, x0 - cx, y0 - cy, x1 - cx, y1 - cy, prm, 2.0f);
    }
};

// ---- public API ---------------------------------------------------------------------

Video::Video() : impl(new Impl) {}
Video::~Video() { shutdown(); delete impl; }

bool Video::init(HWND hwnd, int width, int height) {
    shutdown();
    Impl& I = *impl;
    I.hwnd = hwnd;
    I.w = std::max(1, width); I.h = std::max(1, height);
    if (!I.create_device_objects()) return false;
    if (!I.init_fonts()) log_error("video: DirectWrite unavailable, text disabled\n");
    I.fonts_dirty = true;
    I.ensure_fonts();
    return true;
}

void Video::shutdown() {
    Impl& I = *impl;
    I.release_device_objects();
    for (auto& s : I.slots) { s.cpu.clear(); s.w = s.h = 0; }
    for (auto& chain : I.faces) for (auto*& f : chain) release(f);
    release(I.dw);
    release(I.blob_vs_game); release(I.blob_vs_ov); release(I.blob_ps_ov);
    for (auto*& b : I.blob_ps_game) release(b);
    I.atlas_cpu.clear();
    for (auto& f : I.fonts) f = FontData();
    I.verts.clear();
    I.in_frame = false;
    I.hwnd = nullptr;
}

void Video::resize(int width, int height) {
    Impl& I = *impl;
    if (width <= 0 || height <= 0 || (width == I.w && height == I.h)) return;
    const int old_base = I.font_base();
    I.w = width; I.h = height;
    if (I.font_base() != old_base) I.fonts_dirty = true;
    if (!I.sc) return;
    I.flush();
    release(I.rtv);
    I.ctx->ClearState();
    I.ctx->Flush();
    HRESULT hr = I.sc->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, I.sc_flags);
    if (I.device_lost(hr)) { I.recover(); return; }
    I.create_rtv();
    I.update_screen_cb();
}

int  Video::width() const { return impl->w; }
int  Video::height() const { return impl->h; }
void Video::set_vsync(bool on) { impl->vsync = on; }

void Video::upload_frame(const uint32_t* pixels, int w, int h, int pitch) { impl->upload(0, pixels, w, h, pitch); }
void Video::upload_frame_slot(int slot, const uint32_t* pixels, int w, int h, int pitch) { impl->upload(slot, pixels, w, h, pitch); }

void Video::begin_frame(uint32_t clear_color) {
    Impl& I = *impl;
    I.in_frame = false;
    I.verts.clear();
    if (!I.dev && I.hwnd && GetTickCount64() >= I.next_recover) I.recover();
    if (!I.ctx || !I.rtv) return;
    I.ensure_fonts();
    float c[4] = { ((clear_color >> 16) & 255) / 255.0f, ((clear_color >> 8) & 255) / 255.0f,
                   (clear_color & 255) / 255.0f, (clear_color >> 24) / 255.0f };
    I.ctx->OMSetRenderTargets(1, &I.rtv, nullptr);
    I.ctx->ClearRenderTargetView(I.rtv, c);
    D3D11_VIEWPORT vp = { 0, 0, (float)I.w, (float)I.h, 0, 1 };
    I.ctx->RSSetViewports(1, &vp);
    I.ctx->RSSetState(I.rast);
    float bf[4] = { 0, 0, 0, 0 };
    I.ctx->OMSetBlendState(I.blend, bf, 0xFFFFFFFF);
    I.in_frame = true;
}

void Video::draw_game(int slot, Rect dst, ScaleFilter filter, float alpha, float dim) {
    Impl& I = *impl;
    if (!I.in_frame || slot < 0 || slot > 3 || dst.w <= 0 || dst.h <= 0) return;
    Impl::Slot& s = I.slots[slot];
    int f = (int)filter;
    if (!s.srv || f < 0 || f >= (int)ScaleFilter::Count || alpha <= 0) return;
    I.flush();
    GameCB cb;
    cb.dst[0] = dst.x / I.w * 2 - 1;           cb.dst[1] = 1 - dst.y / I.h * 2;
    cb.dst[2] = (dst.x + dst.w) / I.w * 2 - 1; cb.dst[3] = 1 - (dst.y + dst.h) / I.h * 2;
    cb.src[0] = (float)s.w; cb.src[1] = (float)s.h;
    cb.scale[0] = dst.w / s.w; cb.scale[1] = dst.h / s.h;
    cb.origin[0] = dst.x; cb.origin[1] = dst.y;
    cb.alpha = std::min(alpha, 1.0f);
    cb.dim = std::max(0.0f, std::min(dim, 1.0f));
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(I.ctx->Map(I.cb_game, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &cb, sizeof(cb));
    I.ctx->Unmap(I.cb_game, 0);
    I.ctx->IASetInputLayout(nullptr);
    I.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    I.ctx->VSSetShader(I.vs_game, nullptr, 0);
    I.ctx->VSSetConstantBuffers(0, 1, &I.cb_game);
    I.ctx->PSSetShader(I.ps_game[f], nullptr, 0);
    I.ctx->PSSetConstantBuffers(0, 1, &I.cb_game);
    I.ctx->PSSetShaderResources(0, 1, &s.srv);
    I.ctx->PSSetSamplers(0, 2, I.samp);
    I.ctx->Draw(4, 0);
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
    if (!I.in_frame || fi < 0 || fi >= (int)FontId::Count || !I.atlas_srv || scale <= 0) return;
    const FontData& fd = I.fonts[fi];
    if (fd.glyphs.empty()) return;
    uint32_t col = to_rgba(color);
    bool snap = std::fabs(scale - 1.0f) < 1e-3f;   // unscaled text: pixel-exact glyph placement
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
    if (!I.sc || !I.ctx) return false;
    I.flush();
    ID3D11Texture2D* bb = nullptr, *st = nullptr;
    if (FAILED(I.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    bool ok = false;
    if (SUCCEEDED(I.dev->CreateTexture2D(&d, nullptr, &st))) {
        I.ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(I.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            w = (int)d.Width; h = (int)d.Height;
            pixels.resize((size_t)w * h);
            for (int y = 0; y < h; y++)
                memcpy(&pixels[(size_t)y * w], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * 4);
            I.ctx->Unmap(st, 0);
            ok = true;
        }
    }
    release(st); release(bb);
    return ok;
}

void Video::end_frame() {
    Impl& I = *impl;
    if (!I.in_frame) return;
    I.flush();
    I.in_frame = false;
    UINT flags = (!I.vsync && I.tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = I.sc->Present(I.vsync ? 1 : 0, flags);
    if (I.device_lost(hr)) I.recover();
}
