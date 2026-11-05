// RetroArch slang shader presets through librashader (see slang.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "slang.h"
#define LIBRA_RUNTIME_D3D11
#include "../../third_party/librashader/librashader_ld.h"
#include <algorithm>

namespace {
libra_instance_t g_libra;

template <class T> void safe_release(T*& p) { if (p) p->Release(); p = nullptr; }

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// librashader's message for an error (and the error freed).
std::string message(libra_error_t e) {
    std::string s = "unknown error";
    char* text = nullptr;
    if (g_libra.error_write && g_libra.error_write(e, &text) == 0 && text) {
        s = text;
        if (g_libra.error_free_string) g_libra.error_free_string(&text);
    }
    if (g_libra.error_free) g_libra.error_free(&e);
    return s;
}
}   // namespace

bool SlangChain::available() {
    if (!tried_) {
        tried_ = true;
        g_libra = librashader_load_instance();
        ok_ = g_libra.instance_loaded && g_libra.preset_create && g_libra.d3d11_filter_chain_create &&
              g_libra.d3d11_filter_chain_frame && g_libra.d3d11_filter_chain_free;
    }
    return ok_;
}

bool SlangChain::load(ID3D11Device* dev, const std::wstring& path, std::string* error) {
    if (path.empty()) { release(); return true; }
    if (!available()) { if (error) *error = "librashader.dll not found"; return false; }
    libra_shader_preset_t preset = nullptr;
    if (libra_error_t e = g_libra.preset_create(utf8(path).c_str(), &preset)) {
        if (error) *error = message(e);
        return false;
    }
    filter_chain_d3d11_opt_t opt{};
    opt.version = LIBRASHADER_CURRENT_VERSION;
    libra_d3d11_filter_chain_t chain = nullptr;
    libra_error_t e = g_libra.d3d11_filter_chain_create(&preset, dev, &opt, &chain);
    if (preset && g_libra.preset_free) g_libra.preset_free(&preset);
    if (e) { if (error) *error = message(e); return false; }
    release();
    chain_ = chain;
    dev_ = dev;
    return true;
}

void SlangChain::draw(ID3D11DeviceContext* ctx, ID3D11Texture2D* src, int w, int h, ID3D11RenderTargetView* out,
                      float x, float y, float vw, float vh, unsigned frame) {
    if (!chain_ || w <= 0 || h <= 0) return;
    if (w != src_w_ || h != src_h_ || !src_tex_) {
        safe_release(src_srv_);
        safe_release(src_tex_);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &src_tex_))) return;
        dev_->CreateShaderResourceView(src_tex_, nullptr, &src_srv_);
        src_w_ = w; src_h_ = h;
    }
    D3D11_BOX box{0, 0, 0, (UINT)w, (UINT)h, 1};
    ctx->CopySubresourceRegion(src_tex_, 0, 0, 0, 0, src, 0, &box);
    libra_viewport_t vp{x, y, (uint32_t)std::max(1.0f, vw), (uint32_t)std::max(1.0f, vh)};
    frame_d3d11_opt_t fo{};
    fo.version = LIBRASHADER_CURRENT_VERSION;
    fo.total_subframes = 1;
    fo.current_subframe = 1;
    fo.frames_per_second = 59.9227f;
    auto chain = (libra_d3d11_filter_chain_t)chain_;
    if (libra_error_t e = g_libra.d3d11_filter_chain_frame(&chain, ctx, frame, src_srv_, out, &vp, nullptr, &fo))
        message(e);
}

void SlangChain::release() {
    if (chain_) {
        auto chain = (libra_d3d11_filter_chain_t)chain_;
        g_libra.d3d11_filter_chain_free(&chain);
    }
    chain_ = nullptr;
    safe_release(src_srv_);
    safe_release(src_tex_);
    src_w_ = src_h_ = 0;
}
