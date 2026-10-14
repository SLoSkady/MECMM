# 1. Running code inside the game

**Goal:** get our own code running inside `MirrorsEdgeCatalyst.exe` and draw on top of every frame.

## Environment

| Item | Value |
|---|---|
| Executable | `MirrorsEdgeCatalyst.exe`, x64, image base `0x140000000` |
| Protection | EA DRM: code is encrypted on disk (sections `.ooa`, `.xcode`, `.xtext`, almost no imports). Analyse the **running** process, where the code is already decrypted. |
| Renderer | Direct3D 11 (`d3d11.dll`, `dxgi.dll`) |
| Online | EA's servers were shut down in December 2023; with internet on, the game waits forever after the intro. Start it offline. |
| Saves | `Documents\Mirrors Edge Catalyst\settings\PROF_SAVE*`. A completed save made by another player was not accepted (the game restarted the prologue and overwrote it). |
| Debugger | Cheat Engine's VEH debugger works without crashing the game. |

## Loader: `dinput8.dll` proxy

Windows looks for a DLL in the application's folder before `System32`, unless the DLL is a
*KnownDLL*. `dinput8.dll` (DirectInput) is loaded by the game and is not a KnownDLL, so a
`dinput8.dll` placed next to the game is loaded instead of the system one.

`src/core/proxy.cpp` exports the same functions (`DirectInput8Create`, `DllCanUnloadNow`, ...) and
forwards each call to the real `System32\dinput8.dll`, so input keeps working. `DllMain` only starts a
thread (`src/core/dllmain.cpp`): real work under the loader lock can deadlock the process.

Removing the file restores the vanilla game (`scripts/uninstall.bat`).

## Overlay: hooking `IDXGISwapChain::Present`

Every frame ends with `IDXGISwapChain::Present`. To hook it without knowing the game's swap chain:

1. Create a throw-away D3D11 device + swap chain on a hidden window.
2. Read the function addresses from its vtable (slot 8 = `Present`, slot 13 = `ResizeBuffers`):
   COM vtables are shared by all instances, so these are the game's functions too.
3. Hook them with **MinHook** (`src/overlay/overlay.cpp`).

Inside the hook the swap chain gives the game's device, context and window; Dear ImGui is initialised
on them once and draws the UI before the original `Present` runs. `ResizeBuffers` must release our
view of the back buffer first, or the resize fails.

The same dummy device also provides the `ID3D11DeviceContext` vtable used for the depth hooks
([chapter 4](04-depth-buffer.md)).

## Hotkeys

`GetAsyncKeyState(vk) & 1` ("pressed since the last call") is shared by every program polling the
keyboard and loses presses. The mod remembers the previous "down" state itself and reports the edge
(`KeyPressed` in `overlay.cpp`). Keys are configurable in `mecmm.ini` (`src/core/config.cpp`).
