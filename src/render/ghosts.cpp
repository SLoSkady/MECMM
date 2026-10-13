#include "render/ghosts.h"

#include <d3dcompiler.h>

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "render/depth.h"

namespace ghosts {

namespace {

constexpr float kPi = 3.14159265f;

// --- mesh -------------------------------------------------------------------------------

struct Vertex {
    float pos[3];
    float normal[3];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    // Lat-long sphere of `radius` at `center`, stretched by `cylinder` metres along Y at the
    // equator (cylinder = 0 gives a sphere, > 0 a capsule).
    void AddCapsule(float cx, float cy, float cz, float radius, float cylinder, int rings = 8, int segments = 16) {
        const auto base = static_cast<uint16_t>(vertices.size());
        // rings 0..rings: lower hemisphere, rings+1..2*rings+1: upper hemisphere (shifted up)
        const int rows = 2 * (rings + 1);
        for (int row = 0; row < rows; ++row) {
            bool upper = row > rings;
            int r = upper ? row - 1 : row;  // both hemispheres share the equator angle
            float phi = -kPi / 2 + kPi * r / (2 * rings);
            float y = std::sin(phi), ring = std::cos(phi);
            for (int s = 0; s <= segments; ++s) {
                float theta = 2 * kPi * s / segments;
                float nx = ring * std::cos(theta), nz = ring * std::sin(theta);
                vertices.push_back({{cx + nx * radius, cy + y * radius + (upper ? cylinder : 0), cz + nz * radius},
                                    {nx, y, nz}});
            }
        }
        for (int row = 0; row + 1 < rows; ++row)
            for (int s = 0; s < segments; ++s) {
                auto a = static_cast<uint16_t>(base + row * (segments + 1) + s);
                auto b = static_cast<uint16_t>(a + segments + 1);
                indices.insert(indices.end(), {a, b, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(a + 1), b,
                                               static_cast<uint16_t>(b + 1)});
            }
    }
};

// A runner about 1.75 m tall, feet at the origin, facing +Z.
Mesh BuildFigure() {
    Mesh m;
    constexpr float kBodyRadius = 0.2f;
    m.AddCapsule(0, kBodyRadius + 0.02f, 0, kBodyRadius, 1.05f);  // body: 0.02 .. 1.47 m
    m.AddCapsule(0, 1.6f, 0, 0.13f, 0.0f);                         // head
    m.AddCapsule(0, 1.6f, 0.13f, 0.045f, 0.0f, 4, 8);              // nose: shows where they look
    return m;
}

// --- shaders ----------------------------------------------------------------------------

constexpr char kShader[] = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float3 cameraPosition;
    float hasSceneDepth;
    float2 screenSize;
    float2 pad;
};
cbuffer Object : register(b1) {
    float3 position;
    float yaw;
    float3 color;
    float pad2;
};
Texture2D<float> sceneDepth : register(t0);

struct VS_IN { float3 pos : POSITION; float3 normal : NORMAL; };
struct PS_IN { float4 pos : SV_POSITION; float3 normal : NORMAL; float3 world : WORLDPOS; };

float3 RotateY(float3 v) {
    float s = sin(yaw), c = cos(yaw);
    return float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

PS_IN VS(VS_IN input) {
    PS_IN output;
    output.world = RotateY(input.pos) + position;
    output.normal = RotateY(input.normal);
    output.pos = mul(float4(output.world, 1.0), viewProjection);
    return output;
}

float4 PS(PS_IN input) : SV_Target {
    // Reversed-Z: larger depth = closer. Compare with the game's depth at this pixel
    // (the scene may be rendered at a lower resolution than the screen).
    bool hidden = false;
    if (hasSceneDepth > 0.5) {
        uint w, h;
        sceneDepth.GetDimensions(w, h);
        int2 texel = int2(input.pos.xy / screenSize * float2(w, h));
        float scene = sceneDepth.Load(int3(texel, 0));
        hidden = scene > input.pos.z * 1.01;
    }

    float3 n = normalize(input.normal);
    float3 toCamera = normalize(cameraPosition - input.world);
    float rim = pow(1.0 - saturate(dot(n, toCamera)), 2.0);
    if (hidden)
        return float4(color, 0.15 + 0.35 * rim);  // x-ray silhouette

    float light = 0.45 + 0.55 * saturate(dot(n, normalize(float3(0.3, 0.9, 0.2))));
    return float4(color * light + rim * 0.35, 0.95);
}
)";

struct FrameConstants {
    float viewProjection[4][4];
    float cameraPosition[3];
    float hasSceneDepth;
    float screenSize[2];
    float pad[2];
};

struct ObjectConstants {
    float position[3];
    float yaw;
    float color[3];
    float pad;
};

// --- state ------------------------------------------------------------------------------

ID3D11Device* g_device = nullptr;
bool g_ready = false, g_failed = false;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11InputLayout* g_layout = nullptr;
ID3D11Buffer* g_vertexBuffer = nullptr;
ID3D11Buffer* g_indexBuffer = nullptr;
UINT g_indexCount = 0;
ID3D11Buffer* g_frameConstants = nullptr;
ID3D11Buffer* g_objectConstants = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11DepthStencilState* g_depthState = nullptr;
ID3D11RasterizerState* g_raster = nullptr;

// Our own depth buffer, so the figure's parts occlude each other correctly.
ID3D11Texture2D* g_ownDepth = nullptr;
ID3D11DepthStencilView* g_ownDepthView = nullptr;
UINT g_ownDepthWidth = 0, g_ownDepthHeight = 0;

ID3DBlob* Compile(const char* entry, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, "ghosts", nullptr, nullptr, entry, target, 0, 0, &code, &errors);
    if (FAILED(hr)) Log("ghosts: %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    if (errors) errors->Release();
    return code;
}

ID3D11Buffer* CreateBuffer(UINT size, UINT bind, const void* data, bool dynamic) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = size;
    desc.BindFlags = bind;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA init = {data};
    ID3D11Buffer* buffer = nullptr;
    g_device->CreateBuffer(&desc, data ? &init : nullptr, &buffer);
    return buffer;
}

template <typename T>
void Upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const T& value) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &value, sizeof(value));
        context->Unmap(buffer, 0);
    }
}

bool EnsureOwnDepth(UINT width, UINT height) {
    if (g_ownDepthView && g_ownDepthWidth == width && g_ownDepthHeight == height) return true;
    ReleaseTargets();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_ownDepth)) ||
        FAILED(g_device->CreateDepthStencilView(g_ownDepth, nullptr, &g_ownDepthView)))
        return false;
    g_ownDepthWidth = width;
    g_ownDepthHeight = height;
    depth::Ignore(g_ownDepth);
    return true;
}

// Camera position from a row-vector view matrix [R 0; t 1]: p = -t * R^T.
void CameraPosition(const Camera& camera, float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = -(camera.view[3][0] * camera.view[i][0] + camera.view[3][1] * camera.view[i][1] +
                   camera.view[3][2] * camera.view[i][2]);
}

}  // namespace

bool Init(ID3D11Device* device) {
    if (g_ready || g_failed) return g_ready;
    g_device = device;
    g_failed = true;  // until everything below succeeds

    ID3DBlob* vsCode = Compile("VS", "vs_4_0");
    ID3DBlob* psCode = Compile("PS", "ps_4_0");
    if (!vsCode || !psCode) return false;
    device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &g_vs);
    device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &g_ps);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device->CreateInputLayout(layout, 2, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &g_layout);
    vsCode->Release();
    psCode->Release();

    Mesh mesh = BuildFigure();
    g_vertexBuffer = CreateBuffer(static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex)), D3D11_BIND_VERTEX_BUFFER,
                                  mesh.vertices.data(), false);
    g_indexBuffer = CreateBuffer(static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t)), D3D11_BIND_INDEX_BUFFER,
                                 mesh.indices.data(), false);
    g_indexCount = static_cast<UINT>(mesh.indices.size());
    g_frameConstants = CreateBuffer(sizeof(FrameConstants), D3D11_BIND_CONSTANT_BUFFER, nullptr, true);
    g_objectConstants = CreateBuffer(sizeof(ObjectConstants), D3D11_BIND_CONSTANT_BUFFER, nullptr, true);

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blend, &g_blend);

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_GREATER;  // reversed-Z, like the game's projection
    device->CreateDepthStencilState(&ds, &g_depthState);

    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;  // winding does not matter, our depth buffer sorts the faces
    rs.DepthClipEnable = FALSE;     // infinite projection: nothing to clip against far away
    device->CreateRasterizerState(&rs, &g_raster);

    g_ready = g_vs && g_ps && g_layout && g_vertexBuffer && g_indexBuffer && g_frameConstants && g_objectConstants &&
              g_blend && g_depthState && g_raster;
    g_failed = !g_ready;
    Log("ghosts: init %s (%u indices)", g_ready ? "ok" : "FAILED", g_indexCount);
    return g_ready;
}

void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, float width, float height,
          const Camera& camera, ID3D11ShaderResourceView* sceneDepth, const std::vector<Instance>& instances) {
    if (!g_ready || instances.empty() || !target) return;
    if (!EnsureOwnDepth(static_cast<UINT>(width), static_cast<UINT>(height))) return;

    FrameConstants frame = {};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) frame.viewProjection[r][c] += camera.view[r][k] * camera.projection[k][c];
    CameraPosition(camera, frame.cameraPosition);
    frame.hasSceneDepth = sceneDepth ? 1.0f : 0.0f;
    frame.screenSize[0] = width;
    frame.screenSize[1] = height;
    Upload(context, g_frameConstants, frame);

    context->ClearDepthStencilView(g_ownDepthView, D3D11_CLEAR_DEPTH, 0.0f, 0);  // reversed-Z: 0 = far
    context->OMSetRenderTargets(1, &target, g_ownDepthView);
    D3D11_VIEWPORT viewport = {0, 0, width, height, 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(g_raster);
    const float blendFactor[4] = {};
    context->OMSetBlendState(g_blend, blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(g_depthState, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetInputLayout(g_layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
    context->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R16_UINT, 0);
    context->VSSetShader(g_vs, nullptr, 0);
    context->PSSetShader(g_ps, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* buffers[] = {g_frameConstants, g_objectConstants};
    context->VSSetConstantBuffers(0, 2, buffers);
    context->PSSetConstantBuffers(0, 2, buffers);
    context->PSSetShaderResources(0, 1, &sceneDepth);

    for (const auto& instance : instances) {
        ObjectConstants object = {{instance.position.x, instance.position.y, instance.position.z},
                                  instance.yaw,
                                  {instance.color[0], instance.color[1], instance.color[2]}};
        Upload(context, g_objectConstants, object);
        context->DrawIndexed(g_indexCount, 0, 0);
    }

    // Don't leave the game's depth buffer bound as a texture: it is a render target next frame.
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
    context->OMSetRenderTargets(1, &target, nullptr);
}

void ReleaseTargets() {
    if (g_ownDepthView) g_ownDepthView->Release();
    if (g_ownDepth) g_ownDepth->Release();
    g_ownDepthView = nullptr;
    g_ownDepth = nullptr;
    g_ownDepthWidth = g_ownDepthHeight = 0;
}

}  // namespace ghosts
