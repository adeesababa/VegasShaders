// GPU grass: the strands' pixels (effects/GpuGrass.cpp). Opaque, no alpha test (early depth test works: one shaded sample per pixel
// and strand, the cost the game's alpha-tested cards cannot avoid), the colour the vertex shader worked out. Colour writes RGB only,
// as the game's grass draws (its alpha stays as it was).
// UNOFFICIAL 2026-11-04: plus the deferred flashlight's light the vertex shader handed over, times its shadow (the spot map, 3 x 3 PCF of
// distance-over-reach comparisons as the light pass and the card grass take it), added in linear light.
//
// Build: glslangValidator -V --target-env vulkan1.1 -o GpuGrass.spv GpuGrass.frag (tools/nvr-dxvk/compile-compute.sh)
#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) in vec4 i_color;
layout(location = 1) in vec4 i_spot;        // rgb the flashlight's light (linear), a the distance over its reach
layout(location = 2) in vec4 i_spotClip;    // the point in the shadow map's clip space
layout(location = 3) in vec4 i_spotParams;  // x bias, y PCF radius (texels), z the map's size (0: no shadow), w the gamma
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 6) uniform texture2D t_spotShadow;   // TESR_ShadowSpotlightBuffer0: r the distance to the light over its reach

float spotShadow() {
    const float size = i_spotParams.z;
    if (size < 1.0 || i_spotClip.w <= 0.0) return 1.0;
    const vec2 uv = i_spotClip.xy / i_spotClip.w * vec2(0.5, -0.5) + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    // 2026-11-04 PCF over the light pass's disc (FlashlightDeferred.hlsl FLShadow): 16 taps on a Vogel spiral, i_spotParams.y texels wide
    const ivec2 last = ivec2(int(size) - 1);
    const float bias = i_spotParams.x * (1.0 + 4.0 * i_spot.a);
    vec2 dir = vec2(1.0, 0.0);
    const mat2 golden = mat2(-0.7373688, 0.6754904, -0.6754904, -0.7373688);   // a turn by the golden angle (columns)
    float lit = 0.0;
    for (int i = 0; i < 16; i++) {
        const vec2 p = uv * size + dir * (i_spotParams.y * sqrt((float(i) + 0.5) / 16.0));
        lit += texelFetch(t_spotShadow, clamp(ivec2(p), ivec2(0), last), 0).r + bias > i_spot.a ? 1.0 : 0.0;
        dir = golden * dir;
    }
    return lit / 16.0;
}

void main() {
    vec3 color = i_color.rgb;
    if (i_spot.r + i_spot.g + i_spot.b > 0.0) {
        const float g = i_spotParams.w > 0.0 ? i_spotParams.w : 1.0;
        color = pow(pow(max(color, vec3(0.0)), vec3(g)) + i_spot.rgb * spotShadow(), vec3(1.0 / g));
    }
    o_color = vec4(color, 1.0);
}
