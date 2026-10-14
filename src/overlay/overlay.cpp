#include "overlay/overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/session.h"
#include "game/game.h"
#include "render/depth.h"
#include "render/ghosts.h"

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
bool g_depthDebug = false;
int g_depthPick = -1;  // debug window: -1 = automatic choice, otherwise index into the candidate list

// True once per physical key press. Checks the "is down" bit and remembers the previous
// state; the "pressed since last call" bit of GetAsyncKeyState is shared with every other
// program that polls the keyboard (overlays, launchers) and loses presses.
bool KeyPressed(int vk) {
    static bool wasDown[256] = {};
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !wasDown[vk];
    wasDown[vk] = down;
    return pressed;
}

// Writes the depth buffers the game uses to mecmm.log a few times after start, so they can
// be inspected even without the debug window.
void LogDepthCandidates() {
    static int frame = 0;
    ++frame;
    if (frame != 600 && frame != 3000 && frame != 9000) return;
    auto candidates = depth::Candidates();
    Log("depth: frame %d, %zu depth buffers in use:", frame, candidates.size());
    for (const auto& c : candidates)
        Log("  %ux%u %s mips=%u array=%u msaa=%u srv=%s binds=%u clears=%u clear=%.2f", c.desc.Width, c.desc.Height,
            depth::FormatName(c.desc.Format), c.desc.MipLevels, c.desc.ArraySize, c.desc.SampleDesc.Count,
            c.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE ? "yes" : "no", c.binds, c.clears, c.clearValue);
}

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

// Player colour from a small palette, stable per player id.
void PlayerColor(uint32_t id, float out[3]) {
    static const float kPalette[][3] = {
        {0.30f, 0.85f, 1.00f}, {1.00f, 0.45f, 0.30f}, {0.45f, 1.00f, 0.45f},
        {1.00f, 0.85f, 0.25f}, {0.85f, 0.45f, 1.00f}, {1.00f, 0.40f, 0.70f},
    };
    const float* c = kPalette[id % (sizeof(kPalette) / sizeof(kPalette[0]))];
    out[0] = c[0], out[1] = c[1], out[2] = c[2];
}

// Collects every remote player's interpolated state into `figures` (drawn in 3D afterwards)
// and draws their name and distance. Without 3D figures, also draws a body-sized frame.
void DrawGhosts(const Camera& camera, bool figures3D, std::vector<ghosts::Instance>& figures) {
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

        ghosts::Instance figure{feet, yaw, {}};
        PlayerColor(player.id, figure.color);
        figures.push_back(figure);

        float fx, fy, hx, hy;
        if (!WorldToScreen(camera, feet, screen.x, screen.y, fx, fy) ||
            !WorldToScreen(camera, head, screen.x, screen.y, hx, hy))
            continue;

        float height = std::max(fy - hy, 6.0f);
        float halfWidth = height * 0.2f;
        float cx = (fx + hx) * 0.5f;
        ImVec2 topLeft(cx - halfWidth, hy), bottomRight(cx + halfWidth, hy + height);
        if (!figures3D) {
            draw->AddRectFilled(topLeft, bottomRight, kFill, halfWidth * 0.5f);
            draw->AddRect(topLeft, bottomRight, kFrame, halfWidth * 0.5f, 0, 2.0f);
        }

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
    const Config& config = GetConfig();
    ImGui::TextDisabled("%s: show/hide   %s: depth debug", KeyName(config.keyMenu).c_str(),
                        KeyName(config.keyDebug).c_str());
    ImGui::End();
}

// Pixel shader for previewing a reversed-Z depth buffer in ImGui: near = white, far = black.
// Input layout matches the ImGui DX11 backend's vertex shader output.
constexpr char kDepthPreviewShader[] = R"(
Texture2D<float> depthTexture : register(t0);
SamplerState pointSampler : register(s0);
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
float4 main(PS_INPUT input) : SV_Target {
    float d = depthTexture.Sample(pointSampler, input.uv);
    float distance = 0.06 / max(d, 1e-7);  // reversed-Z: d = near / distance
    float v = saturate(1.0 - log2(distance + 1.0) / 9.0);
    return float4(v, v, v, 1.0);
}
)";

ID3D11PixelShader* DepthPreviewShader() {
    static ID3D11PixelShader* shader = nullptr;
    static bool tried = false;
    if (tried) return shader;
    tried = true;

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kDepthPreviewShader, sizeof(kDepthPreviewShader) - 1, nullptr, nullptr, nullptr, "main",
                            "ps_4_0", 0, 0, &code, &errors);
    if (FAILED(hr)) {
        Log("overlay: depth preview shader failed: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    } else {
        g_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
    }
    if (code) code->Release();
    if (errors) errors->Release();
    return shader;
}

// Lists the depth buffers the game used last frame and shows the one we picked as the scene depth.
void DrawDepthDebug() {
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    depth::Candidate chosen = {};
    ID3D11ShaderResourceView* srv = depth::SceneDepth(g_device, screen.x, screen.y, &chosen, g_depthPick);

    ImGui::SetNextWindowPos(ImVec2(screen.x - 380, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Depth buffers", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Screen %.0fx%.0f", screen.x, screen.y);
    if (g_depthPick < 0)
        ImGui::Text("Pick: automatic   (PgUp/PgDn: choose manually)");
    else
        ImGui::Text("Pick: row %d   (PgUp/PgDn: change, PgUp to the top = automatic)", g_depthPick + 1);
    if (ImGui::BeginTable("candidates", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Format");
        ImGui::TableSetupColumn("Binds");
        ImGui::TableSetupColumn("Clear");
        ImGui::TableSetupColumn("SRV");
        ImGui::TableHeadersRow();
        for (const auto& c : depth::Candidates()) {
            ImGui::TableNextRow();
            if (c.texture == chosen.texture)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 110, 40, 255));
            ImGui::TableNextColumn();
            ImGui::Text("%ux%u%s", c.desc.Width, c.desc.Height, c.desc.ArraySize > 1 ? " [array]" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(depth::FormatName(c.desc.Format));
            ImGui::TableNextColumn();
            ImGui::Text("%u", c.binds);
            ImGui::TableNextColumn();
            if (c.clears) ImGui::Text("%u x %.1f", c.clears, c.clearValue); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE ? "yes" : "no");
        }
        ImGui::EndTable();
    }

    if (srv && DepthPreviewShader()) {
        ImGui::Text("Picked: %ux%u", chosen.desc.Width, chosen.desc.Height);
        float width = 360.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCallback([](const ImDrawList*, const ImDrawCmd*) { g_context->PSSetShader(DepthPreviewShader(), nullptr, 0); },
                          nullptr);
        ImGui::Image(reinterpret_cast<ImTextureID>(srv), ImVec2(width, width * chosen.desc.Height / chosen.desc.Width));
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    } else {
        ImGui::TextDisabled("No scene depth picked yet");
    }
    ImGui::End();
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    depth::EndFrame();  // everything the game drew this frame has been submitted
    if (!g_imguiReady) g_imguiReady = InitImGui(swapChain);

    const Config& config = GetConfig();
    if (KeyPressed(config.keyMenu)) g_visible = !g_visible;
    if (KeyPressed(config.keyDebug)) g_depthDebug = !g_depthDebug;
    if (g_depthDebug && KeyPressed(VK_NEXT)) ++g_depthPick;
    if (g_depthDebug && KeyPressed(VK_PRIOR)) g_depthPick = std::max(g_depthPick - 1, -1);
    LogDepthCandidates();

    if (g_imguiReady) {
        if (!g_rtv) CreateRenderTarget(swapChain);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Remote players: 3D figures + labels. Always on, independent of the info window.
        ImVec2 screen = ImGui::GetIO().DisplaySize;
        Camera camera;
        bool haveCamera = GetCamera(camera);
        bool figures3D = ghosts::Init(g_device);
        std::vector<ghosts::Instance> figures;
        if (haveCamera) DrawGhosts(camera, figures3D, figures);

        if (g_visible) DrawOverlay();
        if (g_depthDebug) DrawDepthDebug();
        ImGui::Render();

        if (haveCamera && figures3D && !figures.empty())
            ghosts::Draw(g_context, g_rtv, screen.x, screen.y, camera,
                         depth::SceneDepth(g_device, screen.x, screen.y), figures);
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
    ghosts::ReleaseTargets();
    return g_originalResizeBuffers(swapChain, count, width, height, format, flags);
}

// Creates a throwaway device + swap chain just to read the vtables of IDXGISwapChain and
// ID3D11DeviceContext (the function addresses are shared by all instances).
bool GetVTables(void**& vtable, void**& contextVTable) {
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
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, &context);
    if (SUCCEEDED(hr)) {
        static void* copy[18];
        memcpy(copy, *reinterpret_cast<void***>(swapChain), sizeof(copy));
        vtable = copy;
        static void* contextCopy[64];
        memcpy(contextCopy, *reinterpret_cast<void***>(context), sizeof(contextCopy));
        contextVTable = contextCopy;
        context->Release();
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
    void** contextVTable = nullptr;
    if (!GetVTables(vtable, contextVTable)) return false;

    if (MH_Initialize() != MH_OK) {
        Log("overlay: MH_Initialize failed");
        return false;
    }
    depth::InstallHooks(contextVTable);  // optional: the overlay works without it
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
