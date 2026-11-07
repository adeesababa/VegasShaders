// UNOFFICIAL 2026-11-02 the reference path tracer's picture (core\RtPathTrace.cpp RenderDebug, [Main.Main.RayTracing] ReferencePT): the
// accumulated mean (linear, the game's units: NVR_RtPathTrace from the RT helper's RtPathTrace.comp) over the finished frame, x PTExposure,
// clamped and gamma-encoded like the game's LDR picture. PTSplit: the left part of the screen keeps the game's own frame (discard).
// Compile: glslangValidator -V --target-env vulkan1.1 -o RtPathTraceShow.spv RtPathTraceShow.frag
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_traced;   // NVR_RtPathTrace
layout(set = 1, binding = 1) uniform texture2D t_albedo;   // (2026-11-03) PTDenoise: the first surface's colour (demodulation), a 0 = sky / frame
layout(set = 1, binding = 2) uniform texture2D t_raw;      // (2026-11-03) PTDenoise: the trace before the denoiser (the sky / frame pixels: exact)
layout(set = 1, binding = 3) uniform texture2D t_viewModel; // (2026-11-07) TESR_DepthBufferViewModel (a stand-in with keepViewModel 0)

layout(push_constant) uniform Push {
    float exposure;      // 0
    uint samplerIndex;   // 4: POINT, CLAMP
    float split;         // 8: 0 = the whole screen; 0.5 = the right half only
    float gamma;         // 12: 1 / 2.2; 1 = the debug views (albedo, normals) as they are
    uint modulate;       // 16: bit 0 = x t_albedo (the traced light came demodulated through the denoiser); bit 1 = into the HDR scene
                         // (PTReplace: not clamped above 1)
    float uvScaleX;      // 20: (2026-11-03) the trace covers the top-left uvScale part (RenderScale < 1, the overlay after the upscale)
    float uvScaleY;      // 24
    uint keepViewModel;  // 28: (2026-11-07) 1 = the first-person model's pixels (view-model depth < 0.9) keep the frame's own; 2 = the same
                         // with reversed depth (> 0.01); 0 = none
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    if (uv.x < pc.split) discard;
    const vec2 tuv = uv * vec2(pc.uvScaleX, pc.uvScaleY);
    // (2026-11-07) the first-person model is not in the traced scene (the trace saw the world behind it): it keeps the game's pixel
    if (pc.keepViewModel != 0u) {
        const float vm = textureLod(sampler2D(t_viewModel, NVR_SAMPLER(pc.samplerIndex)), tuv, 0.0).r;
        if (pc.keepViewModel == 1u ? vm < 0.9 : vm > 0.01) discard;
    }
    vec3 c = textureLod(sampler2D(t_traced, NVR_SAMPLER(pc.samplerIndex)), tuv, 0.0).rgb * pc.exposure;
    if ((pc.modulate & 1u) != 0u) {
        const vec4 a = textureLod(sampler2D(t_albedo, NVR_SAMPLER(pc.samplerIndex)), tuv, 0.0);
        const vec3 raw = textureLod(sampler2D(t_raw, NVR_SAMPLER(pc.samplerIndex)), tuv, 0.0).rgb * pc.exposure;   // (demodulated by 1 there)
        c = mix(raw, c * a.rgb, clamp(a.a, 0.0, 1.0));
    }
    c = (pc.modulate & 2u) != 0u ? max(c, vec3(0.0)) : clamp(c, 0.0, 1.0);
    o_color = vec4(pow(c, vec3(pc.gamma)), 1.0);
}
