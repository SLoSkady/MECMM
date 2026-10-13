// Forwards the real dinput8.dll exports to the system copy, so the game keeps working
// while it loads our DLL under that name.
#include <windows.h>
#include <unknwn.h>
#include <string>

#include "core/log.h"

namespace {

HMODULE g_real = nullptr;

FARPROC Real(const char* name) {
    if (!g_real) {
        wchar_t dir[MAX_PATH];
        GetSystemDirectoryW(dir, MAX_PATH);
        g_real = LoadLibraryW((std::wstring(dir) + L"\\dinput8.dll").c_str());
        if (!g_real) {
            Log("proxy: failed to load system dinput8.dll (error %lu)", GetLastError());
            return nullptr;
        }
    }
    return GetProcAddress(g_real, name);
}

}  // namespace

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto fn = reinterpret_cast<Fn>(Real("DirectInput8Create"));
    return fn ? fn(inst, version, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllCanUnloadNow"));
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    auto fn = reinterpret_cast<Fn>(Real("DllGetClassObject"));
    return fn ? fn(clsid, riid, out) : E_FAIL;
}

HRESULT WINAPI Proxy_DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllRegisterServer"));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(Real("DllUnregisterServer"));
    return fn ? fn() : E_FAIL;
}

}  // extern "C"
