#include <windows.h>

#include "core/log.h"
#include "core/session.h"
#include "overlay/overlay.h"

namespace {

DWORD WINAPI InitThread(LPVOID) {
    Log("MECMM loaded");

    // Wait for the game to load its renderer before hooking it.
    while (!GetModuleHandleW(L"d3d11.dll")) Sleep(100);
    Sleep(1000);

    if (!InstallOverlay()) {
        Log("init: overlay failed, mod is inactive");
        return 0;
    }
    StartSession();
    Log("init: done");
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // DllMain runs under the loader lock: do the real work on a separate thread.
        if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
