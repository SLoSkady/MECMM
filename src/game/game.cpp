#include "game/game.h"

#include <windows.h>
#include <cstdint>
#include <cmath>

namespace {

// Found in research (see research/ and tools/showpos.ps1):
//   [MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x730 -> x, y, z
constexpr uintptr_t kPlayerObjectPtr = 0x23DA020;
constexpr uintptr_t kPositionOffset = 0x730;

// Reads memory without crashing if the address is bad (e.g. during loading).
bool SafeRead(uintptr_t addr, void* out, size_t size) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool GetPlayerPosition(Vec3& out) {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    uintptr_t object = 0;
    if (!SafeRead(base + kPlayerObjectPtr, &object, sizeof(object)) || object == 0) return false;
    if (!SafeRead(object + kPositionOffset, &out, sizeof(out))) return false;
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
