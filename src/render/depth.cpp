#include "render/depth.h"

#include <MinHook.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

#include "core/log.h"

namespace depth {

namespace {

using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                                      ID3D11DepthStencilView*);
using ClearDepthStencilViewFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT,
                                                         UINT8);

OMSetRenderTargetsFn g_originalOMSetRenderTargets = nullptr;
ClearDepthStencilViewFn g_originalClearDepthStencilView = nullptr;

constexpr int kForgetAfterFrames = 120;

struct Tracked {
    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    uint32_t binds = 0, clears = 0;              // current frame
    uint32_t lastBinds = 0, lastClears = 0;      // last finished frame
    float clearValue = -1;
    int framesUnused = 0;
};

std::mutex g_mutex;
std::map<ID3D11Texture2D*, Tracked> g_tracked;  // guarded by g_mutex
ID3D11Texture2D* g_ignored = nullptr;           // guarded by g_mutex

// Shader resource views we created, per tracked texture (Present thread only; entries are
// released together with the texture in EndFrame).
std::map<ID3D11Texture2D*, ID3D11ShaderResourceView*> g_srvs;

// Called from the game's render threads. Must be cheap.
Tracked* Track(ID3D11DepthStencilView* dsv) {
    ID3D11Resource* resource = nullptr;
    dsv->GetResource(&resource);
    if (!resource) return nullptr;
    auto* texture = static_cast<ID3D11Texture2D*>(resource);  // DSVs of the game are 2D textures
    if (texture == g_ignored) {
        resource->Release();
        return nullptr;
    }

    auto it = g_tracked.find(texture);
    if (it == g_tracked.end()) {
        D3D11_RESOURCE_DIMENSION dimension;
        resource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            resource->Release();
            return nullptr;
        }
        Tracked t;
        t.texture = texture;  // keep the reference GetResource gave us
        texture->GetDesc(&t.desc);
        it = g_tracked.emplace(texture, t).first;
    } else {
        resource->Release();
    }
    return &it->second;
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* ctx, UINT count,
                                                ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    if (dsv) {
        std::lock_guard lock(g_mutex);
        if (Tracked* t = Track(dsv)) ++t->binds;
    }
    g_originalOMSetRenderTargets(ctx, count, rtvs, dsv);
}

void STDMETHODCALLTYPE HookedClearDepthStencilView(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv, UINT flags,
                                                   FLOAT value, UINT8 stencil) {
    if (dsv && (flags & D3D11_CLEAR_DEPTH)) {
        std::lock_guard lock(g_mutex);
        if (Tracked* t = Track(dsv)) {
            ++t->clears;
            t->clearValue = value;
        }
    }
    g_originalClearDepthStencilView(ctx, dsv, flags, value, stencil);
}

// Depth formats cannot be sampled directly: the SRV needs the matching "readable" format.
DXGI_FORMAT ReadableFormat(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

}  // namespace

bool InstallHooks(void** vtable) {
    // ID3D11DeviceContext vtable: 33 = OMSetRenderTargets, 53 = ClearDepthStencilView
    if (MH_CreateHook(vtable[33], &HookedOMSetRenderTargets, reinterpret_cast<void**>(&g_originalOMSetRenderTargets)) !=
            MH_OK ||
        MH_CreateHook(vtable[53], &HookedClearDepthStencilView,
                      reinterpret_cast<void**>(&g_originalClearDepthStencilView)) != MH_OK) {
        Log("depth: failed to hook the device context");
        return false;
    }
    Log("depth: context hooks created");
    return true;
}

void Ignore(ID3D11Texture2D* texture) {
    std::lock_guard lock(g_mutex);
    g_ignored = texture;
}

void EndFrame() {
    std::lock_guard lock(g_mutex);
    for (auto it = g_tracked.begin(); it != g_tracked.end();) {
        Tracked& t = it->second;
        t.lastBinds = t.binds;
        t.lastClears = t.clears;
        t.framesUnused = (t.binds || t.clears) ? 0 : t.framesUnused + 1;
        t.binds = t.clears = 0;
        if (t.framesUnused > kForgetAfterFrames) {
            if (auto srv = g_srvs.find(t.texture); srv != g_srvs.end()) {
                if (srv->second) srv->second->Release();
                g_srvs.erase(srv);
            }
            t.texture->Release();
            it = g_tracked.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<Candidate> Candidates() {
    std::vector<Candidate> out;
    {
        std::lock_guard lock(g_mutex);
        for (auto& [texture, t] : g_tracked)
            if (t.lastBinds || t.lastClears) out.push_back({t.texture, t.desc, t.lastBinds, t.lastClears, t.clearValue});
    }
    // Stable order (largest first) so rows don't jump around in the debug window.
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        uint64_t areaA = uint64_t(a.desc.Width) * a.desc.Height, areaB = uint64_t(b.desc.Width) * b.desc.Height;
        return areaA != areaB ? areaA > areaB : a.texture < b.texture;
    });
    return out;
}

ID3D11ShaderResourceView* SceneDepth(ID3D11Device* device, float screenWidth, float screenHeight, Candidate* chosen,
                                     int forceIndex) {
    const float screenAspect = screenWidth / screenHeight;
    const Candidate* best = nullptr;
    auto candidates = Candidates();
    auto readable = [](const Candidate& c) {
        return (c.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && ReadableFormat(c.desc.Format) != DXGI_FORMAT_UNKNOWN &&
               c.desc.SampleDesc.Count == 1 && c.desc.ArraySize == 1;
    };

    if (forceIndex >= 0) {
        if (forceIndex < static_cast<int>(candidates.size()) && readable(candidates[forceIndex]))
            best = &candidates[forceIndex];
    } else {
        for (const auto& c : candidates) {
            float aspect = static_cast<float>(c.desc.Width) / c.desc.Height;
            if (std::fabs(aspect - screenAspect) > 0.02f || !readable(c)) continue;
            if (c.clears == 0 || c.clearValue != 0.0f) continue;  // the scene is cleared to 0 every frame
            if (!best || c.desc.Width * c.desc.Height > best->desc.Width * best->desc.Height ||
                (c.desc.Width == best->desc.Width && c.binds > best->binds))
                best = &c;
        }
    }
    if (!best) return nullptr;
    if (chosen) *chosen = *best;

    auto it = g_srvs.find(best->texture);
    if (it == g_srvs.end()) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = ReadableFormat(best->desc.Format);
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        HRESULT hr = device->CreateShaderResourceView(best->texture, &desc, &srv);
        Log("depth: view for %ux%u %s (binds %u, clears %u, clear value %.2f%s) hr=0x%08lX", best->desc.Width,
            best->desc.Height, FormatName(best->desc.Format), best->binds, best->clears, best->clearValue,
            forceIndex >= 0 ? ", picked manually" : "", hr);
        it = g_srvs.emplace(best->texture, srv).first;  // a failed view is cached as null
    }
    return it->second;
}

const char* FormatName(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
        case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
        case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
        case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TYPELESS";
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
        case DXGI_FORMAT_R16_TYPELESS: return "R16_TYPELESS";
        case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
        default: return "other";
    }
}

}  // namespace depth
