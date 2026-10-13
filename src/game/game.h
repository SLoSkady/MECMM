#pragma once

struct Vec3 {
    float x, y, z;
};

// Faith's world position (Y is up). Returns false if the game data is not available
// (loading screen, main menu, or the pointer path is wrong for this version).
bool GetPlayerPosition(Vec3& out);

// Faith's heading in radians around the Y axis (0 = facing +Z). Same availability as above.
bool GetPlayerYaw(float& out);

// The game's main render camera. Matrices are row-major, row-vector convention (D3D):
// clip = (x, y, z, 1) * view * projection.
struct Camera {
    float view[4][4];
    float projection[4][4];
};

bool GetCamera(Camera& out);

// Projects a world point to screen pixels. Returns false if the point is behind the camera.
bool WorldToScreen(const Camera& camera, const Vec3& world, float screenWidth, float screenHeight, float& x, float& y);
