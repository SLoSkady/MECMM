#pragma once
// Finds the game's scene depth buffer by watching which depth-stencil views the renderer
// binds and clears during a frame (hooks on ID3D11DeviceContext).

#include <d3d11.h>

#include <cstdint>
#include <vector>

namespace depth {

struct Candidate {
    ID3D11Texture2D* texture;  // we hold a reference while it is tracked
    D3D11_TEXTURE2D_DESC desc;
    uint32_t binds;            // OMSetRenderTargets calls with it, last finished frame
    uint32_t clears;           // ClearDepthStencilView calls, last finished frame
    float clearValue;          // depth value of the last clear (0 = reversed-Z, 1 = standard)
};

// `contextVTable` is the vtable of an ID3D11DeviceContext (taken from a dummy device).
bool InstallHooks(void** contextVTable);

// Our own depth buffers (e.g. for the 3D figures) must not be mistaken for the game's.
void Ignore(ID3D11Texture2D* texture);

// Call once per frame (from Present): closes the frame's statistics.
void EndFrame();

// Depth buffers seen during the last frame, largest first.
std::vector<Candidate> Candidates();

// Best guess for the main scene depth buffer: same aspect ratio as the screen, cleared to 0
// (reversed-Z, like the game's camera projection), largest, most used. The scene may be rendered
// below screen resolution ("render quality" setting), so the size does not have to match the screen.
// `forceIndex` >= 0 picks Candidates()[forceIndex] instead (for the debug window).
// Returns a shader resource view for it (owned by this module) or null.
ID3D11ShaderResourceView* SceneDepth(ID3D11Device* device, float screenWidth, float screenHeight,
                                     Candidate* chosen = nullptr, int forceIndex = -1);

const char* FormatName(DXGI_FORMAT format);

}  // namespace depth
