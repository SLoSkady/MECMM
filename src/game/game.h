#pragma once

struct Vec3 {
    float x, y, z;
};

// Faith's world position (Y is up). Returns false if the game data is not available
// (loading screen, main menu, or the pointer path is wrong for this version).
bool GetPlayerPosition(Vec3& out);
