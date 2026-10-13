#include "game/game.h"

#include <windows.h>
#include <cstdint>
#include <cmath>

namespace {

// Found in research (see docs/research/findings.md):
//   [MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x700 -> 4x4 transform of the character root
//   rows: right (+0x00), up (+0x10), forward (+0x20), translation (+0x30)
constexpr uintptr_t kPlayerObjectPtr = 0x23DA020;
constexpr uintptr_t kTransformOffset = 0x700;
constexpr uintptr_t kForwardOffset = kTransformOffset + 0x20;
constexpr uintptr_t kPositionOffset = kTransformOffset + 0x30;

// Reads memory without crashing if the address is bad (e.g. during loading).
bool SafeRead(uintptr_t addr, void* out, size_t size) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPlayerVec3(uintptr_t offset, Vec3& out) {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    uintptr_t object = 0;
    if (!SafeRead(base + kPlayerObjectPtr, &object, sizeof(object)) || object == 0) return false;
    if (!SafeRead(object + offset, &out, sizeof(out))) return false;
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

}  // namespace

bool GetPlayerPosition(Vec3& out) { return ReadPlayerVec3(kPositionOffset, out); }

bool GetPlayerYaw(float& out) {
    Vec3 forward;
    if (!ReadPlayerVec3(kForwardOffset, forward)) return false;
    out = std::atan2(forward.x, forward.z);
    return true;
}
