#include "overlay/overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "game/game.h"
#include "core/log.h"

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

void DrawOverlay() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("MECMM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    Vec3 pos;
    if (GetPlayerPosition(pos)) {
        ImGui::Text("X: %8.2f", pos.x);
        ImGui::Text("Y: %8.2f", pos.y);
        ImGui::Text("Z: %8.2f", pos.z);
    } else {
        ImGui::TextDisabled("Faith position: unavailable");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Insert: show/hide");
    ImGui::End();
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    if (!g_imguiReady) g_imguiReady = InitImGui(swapChain);

    if (GetAsyncKeyState(VK_INSERT) & 1) g_visible = !g_visible;

    if (g_imguiReady && g_visible) {
        if (!g_rtv) CreateRenderTarget(swapChain);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawOverlay();
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
