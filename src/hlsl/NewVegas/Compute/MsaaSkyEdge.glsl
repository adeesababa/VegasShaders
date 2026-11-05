// UNOFFICIAL 2026-11-05: Effects/Includes/MsaaSkyEdge.hlsl line for line (the 2026-11-05 MsaaEdgeFix part and the debug view; the
// 2026-11-04 SkyShare was never ported: the Vulkan composites had no sky shares at all before this). The includer declares t_rendered,
// t_skycov (TESR_SkyCoverage) and a uniform block cb with reciprocalResolution and gameMultisample, defines POINT_LOD, readDepthLod
// and farZ (Common.glsl gives linearize and saturate).

bool MsaaEdgeFixOn() { return cb.gameMultisample.x > 1.0 && cb.gameMultisample.y > 0.5 && cb.gameMultisample.z > 0.5; }

float MsaaSkyCoverage(vec2 at) { return POINT_LOD(t_skycov, at).r; }

const float MsaaAllSky = 0.998;
const float MsaaNoSky = 0.002;

vec3 MsaaSkyBehind(vec2 at, vec3 color) {
    vec3 skySum = vec3(0.0);
    float skyCount = 0.0;
    const vec2 offsets[4] = vec2[4](vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1));
    const float radii[4] = float[4](1.0, 2.0, 4.0, 8.0);
    for (int r = 0; r < 4; r++) {
        if (skyCount < 2.0) {
            for (int i = 0; i < 4; i++) {
                vec2 p = at + offsets[i] * radii[r] * cb.reciprocalResolution.xy;
                if (MsaaSkyCoverage(p) >= MsaaAllSky) { skySum += POINT_LOD(t_rendered, p).rgb; skyCount += 1.0; }
            }
        }
    }
    return linearize(max(skyCount > 0.0 ? skySum / skyCount : color, vec3(0.0)));
}

float MsaaSurfaceDepth(vec2 at, float fallback) {
    float nearest = fallback;
    const vec2 offsets[8] = vec2[8](vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1), vec2(1, 1), vec2(-1, 1), vec2(1, -1), vec2(-1, -1));
    for (int r = 1; r <= 2; r++) {
        for (int i = 0; i < 8; i++) {
            float d = readDepthLod(at + offsets[i] * float(r) * cb.reciprocalResolution.xy);
            if (d < farZ * 0.996) nearest = min(nearest, d);
        }
    }
    return nearest;
}

bool MsaaEdgeDebugOn() { return cb.gameMultisample.w > 0.5; }
vec4 MsaaEdgeDebug(vec2 at, float depth, vec3 color) {
    float share = cb.gameMultisample.x > 1.0 && cb.gameMultisample.y > 0.5 ? MsaaSkyCoverage(at) : 0.0;
    return vec4(depth >= farZ * 0.996 ? 1.0 : 0.0, share, saturate(dot(color, vec3(0.2126, 0.7152, 0.0722))) * 0.33, 1.0);
}
