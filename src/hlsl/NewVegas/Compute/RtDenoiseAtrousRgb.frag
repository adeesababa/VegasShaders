// UNOFFICIAL 2026-10-29 shared RT denoiser (core\RtDenoise.h), RGB mode, pass 2 (run 3 times): the edge-stopping a-trous of
// RtDenoiseAtrous.frag on rgb, the value weight from the luma's temporal variance. In / out: A = (rgb, history length),
// B = (luma second moment, view depth, 0, 0); the last iteration writes the caller's target (rgb in A's place, B to a scratch).
// Compile: glslangValidator -V --target-env vulkan1.1 -o RtDenoiseAtrousRgb.spv RtDenoiseAtrousRgb.frag
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_a;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_b;
// (2026-11-07) Settings.Guide: the game's own lit frame (as stored in the scene: x^(1/2.2)) and the first surface's colour (linear); their
// ratio is the game's shading (normal maps, cracks, creases), and a tap whose shading differs from this pixel's is weighed down, so the
// smoothing stops at the edges the game's picture shows instead of only where the smooth normals or the depth change (stand-ins when off)
layout(set = 1, binding = 3) uniform texture2D t_guideFrame;
layout(set = 1, binding = 4) uniform texture2D t_guideAlbedo;

layout(push_constant) uniform Push {
    vec4 params;   // x step, y depth sigma, z normal power, w value sigma
    vec4 size;     // x 1 / width, y 1 / height, w 1: the 3 x 3 kernel
    uint samplerIndex;
    float guideSigma;   // (2026-11-07) Settings.GuideSigma: the relative shading difference that weighs a tap down by 1 / e (0 = no guide)
    uint pad[2];
} pc;

float GuideShade(vec2 at) {
    const vec3 f = pow(max(textureLod(sampler2D(t_guideFrame, NVR_SAMPLER(pc.samplerIndex)), at, 0.0).rgb, vec3(0.0)), vec3(2.2));
    const vec4 a = textureLod(sampler2D(t_guideAlbedo, NVR_SAMPLER(pc.samplerIndex)), at, 0.0);
    return a.a < 0.5 ? -1.0 : luma(f) / max(luma(a.rgb), 0.02);
}

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_a;
layout(location = 1) out vec4 o_b;

const float kernel[3] = float[3](3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
const float kernel3[2] = float[2](1.0 / 2.0, 1.0 / 4.0);   // the 3 x 3 kernel (Settings.Kernel 3): 9 taps instead of 25

void main() {
    const vec4 ca = textureLod(sampler2D(t_a, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    const vec4 cb = textureLod(sampler2D(t_b, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    const vec3 n = normalize(textureLod(sampler2D(t_normals, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).xyz * 2.0 - 1.0);
    const float z = cb.g;
    if (z <= 0.0) { o_a = ca; o_b = cb; return; }
    const float l = luma(ca.rgb);
    const float variance = max(cb.r - l * l, 0.0);
    const float valueSigma = pc.params.w * sqrt(variance + (ca.a < 4.0 ? 0.05 : 0.0)) + 1.0e-3;
    const float zSlope = max(abs(dFdx(z)), abs(dFdy(z))) * pc.params.x + 1.0e-2 * z;
    const float g0 = pc.guideSigma > 0.0 ? GuideShade(uv) : -1.0;

    vec3 sum = vec3(0.0);
    float moment = 0.0, weights = 0.0;
    const int reach = pc.size.w > 0.5 ? 1 : 2;
    for (int y = -reach; y <= reach; y++) {
        for (int x = -reach; x <= reach; x++) {
            const vec2 tapUv = uv + vec2(x, y) * pc.params.x * pc.size.xy;
            const vec4 ta = textureLod(sampler2D(t_a, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0);
            const vec4 tb = textureLod(sampler2D(t_b, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0);
            if (tb.g <= 0.0) continue;
            const vec3 tn = normalize(textureLod(sampler2D(t_normals, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0).xyz * 2.0 - 1.0);
            float w = reach == 1 ? kernel3[abs(x)] * kernel3[abs(y)] : kernel[abs(x)] * kernel[abs(y)];
            w *= exp(-abs(tb.g - z) / (pc.params.y * zSlope * length(vec2(x, y)) + 1.0e-3));
            w *= pow(max(dot(n, tn), 0.0), pc.params.z);
            w *= exp(-abs(luma(ta.rgb) - l) / valueSigma);
            if (g0 >= 0.0 && (x != 0 || y != 0)) {
                const float gt = GuideShade(tapUv);
                if (gt >= 0.0) w *= exp(-abs(gt - g0) / (pc.guideSigma * 0.5 * (gt + g0) + 1.0e-4));
            }
            sum += ta.rgb * w;
            moment += tb.r * w;
            weights += w;
        }
    }
    o_a = vec4(weights > 0.0 ? sum / weights : ca.rgb, ca.a);
    o_b = vec4(weights > 0.0 ? moment / weights : cb.r, z, 0.0, 0.0);
}
