#include "overlay/overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/log.h"
#include "core/session.h"
#include "game/game.h"

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_originalPresent = nullptr;
ResizeBuffersFn g_originalResizeBuffers = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_imguiReady = false;
bool g_visible = true;

void CreateRenderTarget(IDXGISwapChain* swapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

bool InitImGui(IDXGISwapChain* swapChain) {
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) return false;
    g_device->GetImmediateContext(&g_context);

    DXGI_SWAP_CHAIN_DESC desc;
    swapChain->GetDesc(&desc);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // don't write imgui.ini into the game folder
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(desc.OutputWindow);
    ImGui_ImplDX11_Init(g_device, g_context);
    CreateRenderTarget(swapChain);

    Log("overlay: ImGui initialized, window %p, %ux%u", desc.OutputWindow, desc.BufferDesc.Width, desc.BufferDesc.Height);
    return true;
}

void DrawNetwork(const Vec3* me) {
    const net::Client& client = Session();
    switch (client.GetStatus()) {
        case net::Status::Connected:
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Online (#%u)", client.GetPlayerId());
            break;
        case net::Status::Connecting: {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Connecting...");
            std::string error = client.GetError();
            if (!error.empty()) ImGui::TextDisabled("%s", error.c_str());
            break;
        }
        case net::Status::Error:
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", client.GetError().c_str());
            return;
        case net::Status::Stopped:
            ImGui::TextDisabled("Offline");
            return;
    }

    auto players = client.Players();
    if (players.empty()) {
        ImGui::TextDisabled("No other players");
        return;
    }
    for (const auto& p : players) {
        if (me) {
            float dx = p.pos[0] - me->x, dy = p.pos[1] - me->y, dz = p.pos[2] - me->z;
            ImGui::Text("%-16s %7.1f m", p.name.c_str(), std::sqrt(dx * dx + dy * dy + dz * dz));
        } else {
            ImGui::Text("%s", p.name.c_str());
        }
    }
}

// Draws every remote player where they are in the world: a body-sized frame with the
// name above and the distance below.
void DrawGhosts() {
    Camera camera;
    if (!GetCamera(camera)) return;

    constexpr float kBodyHeight = 1.75f;  // metres
    const ImU32 kFrame = IM_COL32(80, 220, 255, 230);
    const ImU32 kFill = IM_COL32(80, 220, 255, 40);
    const ImU32 kText = IM_COL32(255, 255, 255, 255);
    const ImU32 kShadow = IM_COL32(0, 0, 0, 200);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    uint64_t drawTime = net::NowMs() - net::kInterpolationDelayMs;

    Vec3 me;
    bool haveMe = GetPlayerPosition(me);

    for (const auto& player : Session().Players()) {
        float p[3], yaw;
        player.Sample(drawTime, p, yaw);
        Vec3 feet{p[0], p[1], p[2]};
        Vec3 head{p[0], p[1] + kBodyHeight, p[2]};

        float fx, fy, hx, hy;
        if (!WorldToScreen(camera, feet, screen.x, screen.y, fx, fy) ||
            !WorldToScreen(camera, head, screen.x, screen.y, hx, hy))
            continue;

        float height = std::max(fy - hy, 6.0f);
        float halfWidth = height * 0.2f;
        float cx = (fx + hx) * 0.5f;
        ImVec2 topLeft(cx - halfWidth, hy), bottomRight(cx + halfWidth, hy + height);
        draw->AddRectFilled(topLeft, bottomRight, kFill, halfWidth * 0.5f);
        draw->AddRect(topLeft, bottomRight, kFrame, halfWidth * 0.5f, 0, 2.0f);

        auto label = [&](const char* text, float y) {
            ImVec2 size = ImGui::CalcTextSize(text);
            ImVec2 at(cx - size.x * 0.5f, y);
            draw->AddText(ImVec2(at.x + 1, at.y + 1), kShadow, text);
            draw->AddText(at, kText, text);
        };
        label(player.name.c_str(), hy - ImGui::GetTextLineHeight() - 2);
        if (haveMe) {
            float dx = p[0] - me.x, dy = p[1] - me.y, dz = p[2] - me.z;
            char distance[32];
            snprintf(distance, sizeof(distance), "%.0f m", std::sqrt(dx * dx + dy * dy + dz * dz));
            label(distance, bottomRight.y + 2);
        }
    }
}

void DrawOverlay() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("MECMM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    Vec3 pos;
    bool havePos = GetPlayerPosition(pos);
    if (havePos) {
        ImGui::Text("X: %8.2f", pos.x);
        ImGui::Text("Y: %8.2f", pos.y);
        ImGui::Text("Z: %8.2f", pos.z);
    } else {
        ImGui::TextDisabled("Faith position: unavailable");
    }
    ImGui::Separator();
    DrawNetwork(havePos ? &pos : nullptr);
    ImGui::Separator();
    ImGui::TextDisabled("Insert: show/hide");
    ImGui::End();
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    if (!g_imguiReady) g_imguiReady = InitImGui(swapChain);

    if (GetAsyncKeyState(VK_INSERT) & 1) g_visible = !g_visible;

    if (g_imguiReady) {
        if (!g_rtv) CreateRenderTarget(swapChain);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawGhosts();  // always: they are part of the game, not of the info window
        if (g_visible) DrawOverlay();
        ImGui::Render();

        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    return g_originalPresent(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT count, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags) {
    // The back buffer is recreated on resize; our view of it must be released first.
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    return g_originalResizeBuffers(swapChain, count, width, height, format, flags);
}

// Creates a throwaway device + swap chain just to read the addresses of
// Present/ResizeBuffers from the vtable (they are shared by all swap chains).
bool GetSwapChainVTable(void**& vtable) {
    WNDCLASSEXW wc = {sizeof(wc), CS_HREDRAW | CS_VREDRAW, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr)};
    wc.lpszClassName = L"MECMM_Dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                              wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        static void* copy[18];
        memcpy(copy, *reinterpret_cast<void***>(swapChain), sizeof(copy));
        vtable = copy;
        swapChain->Release();
        device->Release();
    } else {
        Log("overlay: dummy D3D11 device failed, hr=0x%08lX", hr);
    }
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return SUCCEEDED(hr);
}

}  // namespace

bool InstallOverlay() {
    void** vtable = nullptr;
    if (!GetSwapChainVTable(vtable)) return false;

    if (MH_Initialize() != MH_OK) {
        Log("overlay: MH_Initialize failed");
        return false;
    }
    // IDXGISwapChain vtable: 8 = Present, 13 = ResizeBuffers
    if (MH_CreateHook(vtable[8], &HookedPresent, reinterpret_cast<void**>(&g_originalPresent)) != MH_OK ||
        MH_CreateHook(vtable[13], &HookedResizeBuffers, reinterpret_cast<void**>(&g_originalResizeBuffers)) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("overlay: failed to hook Present/ResizeBuffers");
        return false;
    }
    Log("overlay: hooks installed (Present at %p)", vtable[8]);
    return true;
}
