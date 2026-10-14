#include "game/game.h"

#include <windows.h>
#include <cstdint>
#include <cmath>

namespace {

// Found in research (see docs/research/02-player-position.md and 03-camera.md):
//   [MirrorsEdgeCatalyst.exe + 0x23DA020] + 0x700 -> 4x4 transform of the character root
//   rows: right (+0x00), up (+0x10), forward (+0x20), translation (+0x30)
constexpr uintptr_t kPlayerObjectPtr = 0x23DA020;
constexpr uintptr_t kTransformOffset = 0x700;
constexpr uintptr_t kForwardOffset = kTransformOffset + 0x20;
constexpr uintptr_t kPositionOffset = kTransformOffset + 0x30;

//   [MirrorsEdgeCatalyst.exe + 0x2563A10] -> render view of the main camera
constexpr uintptr_t kRenderViewPtr = 0x2563A10;
constexpr uintptr_t kViewMatrixOffset = 0x630;
constexpr uintptr_t kProjectionOffset = 0x6F0;

// Reads memory without crashing if the address is bad (e.g. during loading).
bool SafeRead(uintptr_t addr, void* out, size_t size) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Follows [exe + staticOffset] and reads `size` bytes at `offset` in the pointed-to object.
bool ReadThrough(uintptr_t staticOffset, uintptr_t offset, void* out, size_t size) {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    uintptr_t object = 0;
    if (!SafeRead(base + staticOffset, &object, sizeof(object)) || object == 0) return false;
    return SafeRead(object + offset, out, size);
}

bool ReadPlayerVec3(uintptr_t offset, Vec3& out) {
    if (!ReadThrough(kPlayerObjectPtr, offset, &out, sizeof(out))) return false;
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

bool GetCamera(Camera& out) {
    if (!ReadThrough(kRenderViewPtr, kViewMatrixOffset, out.view, sizeof(out.view)) ||
        !ReadThrough(kRenderViewPtr, kProjectionOffset, out.projection, sizeof(out.projection)))
        return false;
    // A perspective projection scales x and y; anything else means we read garbage.
    return std::isfinite(out.projection[0][0]) && out.projection[0][0] > 0.01f && out.projection[1][1] > 0.01f;
}

bool WorldToScreen(const Camera& camera, const Vec3& world, float screenWidth, float screenHeight, float& x, float& y) {
    const float p[4] = {world.x, world.y, world.z, 1.0f};
    float v[4] = {}, clip[4] = {};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) v[c] += p[r] * camera.view[r][c];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) clip[c] += v[r] * camera.projection[r][c];

    if (clip[3] < 0.05f) return false;  // behind the camera (or too close to it)
    x = (clip[0] / clip[3] * 0.5f + 0.5f) * screenWidth;
    y = (0.5f - clip[1] / clip[3] * 0.5f) * screenHeight;
    return true;
}
