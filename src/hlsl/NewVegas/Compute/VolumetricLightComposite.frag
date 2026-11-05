// VolumetricLight technique Composite (0) as a Vulkan pass (needs NVR DXVK): Effects/VolumetricLight.fx.hlsl CompositePS, MarchAirAt,
// MsaaAirMix and CompositeAir line for line, full resolution, through the frame chain. effects/VolumetricLight.cpp ExtRender.
// (UNOFFICIAL 2026-11-05: the sky shares of the game's MSAA, t_skycov, MsaaSkyEdge.glsl; the 2026-11-04 D3D9 SkyShare had no port here.)
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer (POINT, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_depth;      // TESR_DepthBuffer (POINT, CLAMP)
layout(set = 1, binding = 2) uniform texture2D t_buffer;     // TESR_VolumetricLightBuffer (POINT, CLAMP)
layout(set = 1, binding = 3) uniform texture2D t_skycov;     // TESR_SkyCoverage (POINT, CLAMP; the depth when the game's MSAA is off)

#define VL_DEPTH
#define VL_COMPOSITE
#include "VolumetricLight.glsl"
#include "MsaaSkyEdge.glsl"

// Depth-aware bilinear upsample from the half-resolution buffer (texel k stands for its centre).
vec2 MarchAirAt(vec2 at, float depth) {
    vec2 position = at / LowTexel - 0.5;
    vec2 base = floor(position);
    vec2 fraction = position - base;
    float depth01 = depth / farZ;
    float tolerance = max(depth01 * 0.04, 0.00002);
    vec2 air = vec2(0.0);
    float weights = 1.0e-5;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            vec4 tap = POINT_LOD(t_buffer, (base + vec2(x, y) + 0.5) * LowTexel);
            float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) * (y != 0 ? fraction.y : 1.0 - fraction.y);
            weight /= 1.0 + abs(tap.b - depth01) / tolerance;
            air += tap.rg * weight;
            weights += weight;
        }
    }
    return air / weights;
}

vec2 MsaaAirMix(vec2 at, float depth, vec2 air) {
    float share = MsaaSkyCoverage(at);
    if (share <= MsaaNoSky) return air;
    if (depth < farZ * 0.996) return mix(air, MarchAirAt(at, farZ), share);
    if (share >= MsaaAllSky) return air;
    float surface = MsaaSurfaceDepth(at, farZ);
    if (surface >= farZ * 0.996) return air;
    return mix(MarchAirAt(at, surface), air, share);
}

void main() {
    vec4 color = POINT_LOD(t_rendered, uv);
    float depth = readDepthLod(uv);
    if (MsaaEdgeDebugOn()) { o_color = MsaaEdgeDebug(uv, depth, color.rgb); return; }
    vec2 air = MarchAirAt(uv, depth);
    if (MsaaEdgeFixOn()) air = MsaaAirMix(uv, depth, air);
    o_color = CompositeAir(uv, color, air);
}
