#pragma once
// Draws remote players as simple 3D figures (capsule body, head, "nose" showing the heading)
// in the game world, using the game's camera and its scene depth buffer for occlusion:
// visible parts are solid, parts behind walls are drawn as a faint silhouette.

#include <d3d11.h>

#include <cstdint>
#include <vector>

#include "game/game.h"

namespace ghosts {

struct Instance {
    Vec3 position;  // feet
    float yaw;      // radians around Y, 0 = facing +Z
    float color[3];
};

// Creates shaders and meshes on the game's device. Safe to call every frame (does the work once).
bool Init(ID3D11Device* device);

// Renders into `target` (the back buffer). `sceneDepth` may be null: then nothing is occluded.
void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, float width, float height,
          const Camera& camera, ID3D11ShaderResourceView* sceneDepth, const std::vector<Instance>& instances);

// Releases size-dependent resources (call before the swap chain is resized).
void ReleaseTargets();

}  // namespace ghosts
