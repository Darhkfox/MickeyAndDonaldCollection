// RetroArch slang shader presets (.slangp) through librashader, loaded at run time from librashader.dll next to the
// exe (third_party/librashader). Without the DLL the game runs as before, on its built-in filters. Windows only.
#pragma once
#include <d3d11.h>
#include <string>

class SlangChain {
public:
    // Whether librashader.dll could be loaded (tried once).
    bool available();
    // Builds the chain for a preset file; false (and why) on failure, the old chain kept. An empty path frees the
    // chain (built-in filters again).
    bool load(ID3D11Device* dev, const std::wstring& path, std::string* error);
    bool active() const { return chain_ != nullptr; }
    // Runs the chain on the game picture (the top-left w x h of src, copied to a texture of exactly that size: the
    // presets take the source's size from it) into the viewport rectangle of out.
    void draw(ID3D11DeviceContext* ctx, ID3D11Texture2D* src, int w, int h, ID3D11RenderTargetView* out, float x,
              float y, float vw, float vh, unsigned frame);
    void release();   // the chain and its textures (the device going away)

private:
    bool tried_ = false, ok_ = false;
    void* chain_ = nullptr;   // libra_d3d11_filter_chain_t
    ID3D11Device* dev_ = nullptr;
    ID3D11Texture2D* src_tex_ = nullptr;
    ID3D11ShaderResourceView* src_srv_ = nullptr;
    int src_w_ = 0, src_h_ = 0;
};
