// WetWorld as Vulkan passes (needs NVR DXVK; effects/WetWorld.cpp ExtRender): the part shared by WetWorldMap.frag and WetWorld.frag,
// line for line the helpers of Effects\WetWorld.fx.hlsl, Includes\Depth.hlsl and Includes\Normals.hlsl (and the constants at the top
// of WetWorld.fx.hlsl the helpers use). Included after the shader's own texture and push declarations (t_depth, t_depthVM, t_ortho,
// t_normals, pc.sLin, pc.sPoint) and after Common.glsl.
//
// The constants block holds the registers the D3D9 path uploads (ExtRender copies them every frame). A matrix is its four registers:
// HLSL M[i][j] is register j's component i, so it is c.M[j][i] here; mul(v, M) is v * c.M and mul(M, v) is c.M * v (Snow.frag).

layout(set = 2, binding = 0, std140) uniform Consts {
    mat4 shadowToLight;           // TESR_ShadowCameraToLightTransformOrtho
    mat4 projection;              // TESR_ProjectionTransform
    mat4 invProjection;           // TESR_InvProjectionTransform
    mat4 view;                    // TESR_ViewTransform
    mat4 invView;                 // TESR_InvViewTransform
    vec4 cameraData;              // TESR_CameraData (x near, y far)
    vec4 depthConstants;          // TESR_DepthConstants
    vec4 cameraPosition;          // TESR_CameraPosition
    vec4 reciprocalResolution;    // TESR_ReciprocalResolution
    vec4 gameTime;                // TESR_GameTime
    vec4 sunDirection;            // TESR_SunDirection
    vec4 sunPosition;             // TESR_SunPosition
    vec4 sunAmount;               // TESR_SunAmount
    vec4 sunColor;                // TESR_SunColor
    vec4 sunAmbient;              // TESR_SunAmbient
    vec4 skyData;                 // TESR_SkyData
    vec4 skyColor;                // TESR_SkyColor
    vec4 skyLowColor;             // TESR_SkyLowColor
    vec4 horizonColor;            // TESR_HorizonColor
    vec4 orthoData;               // TESR_OrthoData
    vec4 waterSettings;           // TESR_WaterSettings
    vec4 wetWorldData;            // TESR_WetWorldData
    vec4 wetWorldLook;            // TESR_WetWorldLook
    vec4 lightPosition[12];       // TESR_LightPosition
    vec4 shadowLightPosition[12]; // TESR_ShadowLightPosition
    vec4 lightColor[24];          // TESR_LightColor
    vec4 renderScale;             // TESR_RenderScale (xy = Scale, zw = 1 / Scale)
} c;

vec2 rectToScreen(vec2 ruv) { return ruv * c.renderScale.zw; }
vec2 screenToRect(vec2 s01) { return s01 * c.renderScale.xy; }
vec2 clampToRect(vec2 ruv, vec2 texel) {
    return clamp(ruv, 0.5 * texel, max(c.renderScale.xy - 0.5 * texel, 0.5 * texel));
}

#define farZ (c.cameraData.y)
#define nearZ (c.cameraData.x)
#define DrawD (c.orthoData.x * 0.9)

// Constants of WetWorld.fx.hlsl used by the helpers below (the rest sit in WetWorld.frag).
const float PuddleBlurRadius = 12.0;
const float PuddleBlurDepthTolerance = 0.08;
const float CurvatureWorldRadius = 100.0;
const float CurvatureDepthSign = 1.0;
const float CurvatureSurfaceTolerance = 0.0005;
const float DepressionMinCurvature = 0.25;
const float DepressionFullCurvature = 2.0;
const float DepressionSlopeFadeStart = 0.906;
const float DepressionSlopeCutoff = 0.707;
const float DepressionMaxDepthJump = 80.0;

// HLSL smoothstep(a, b, x) for any a and b (GLSL's is undefined when a > b)
float smoothstepH(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
vec2 saturate(vec2 x) { return clamp(x, vec2(0.0), vec2(1.0)); }
vec4 saturate(vec4 x) { return clamp(x, vec4(0.0), vec4(1.0)); }

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.sLin)), coord).x * farZ;
}

float projectedDepthFromLinear(float linear01) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

vec3 reconstructPosition(vec2 ruv) {
    vec2 s = rectToScreen(ruv);
    float x = s.x * 2.0 - 1.0;
    float y = (1.0 - s.y) * 2.0 - 1.0;
    float z = projectedDepthFromLinear(texture(sampler2D(t_depth, NVR_SAMPLER(pc.sLin)), ruv).x);
    vec4 viewSpace = vec4(x, y, z, 1.0) * c.invProjection;
    viewSpace /= viewSpace.w;
    return viewSpace.xyz;
}

vec3 projectPosition(vec3 position) {
    vec4 projection = vec4(position, 1.0) * c.projection;
    projection.xyz /= projection.w;
    projection.x = projection.x * 0.5 + 0.5;
    projection.y = 0.5 - 0.5 * projection.y;
    return projection.xyz;
}

vec3 toWorld(vec2 ruv) {
    vec2 s = rectToScreen(ruv);
    vec3 v = c.view[2].xyz;
    v += (1.0 / c.projection[0][0] * (2.0 * s.x - 1.0)) * c.view[0].xyz;
    v += (-1.0 / c.projection[1][1] * (2.0 * s.y - 1.0)) * c.view[1].xyz;
    return v;
}

vec3 GetWorldNormal(vec2 uv) {
    vec4 normal = vec4(texture(sampler2D(t_normals, NVR_SAMPLER(pc.sPoint)), uv).xyz * 2.0 - 1.0, 1.0);
    return (c.view * normal).xyz;
}

float SampleOrthoDepth(vec2 uv) {
    return textureLod(sampler2D(t_ortho, NVR_SAMPLER(pc.sPoint)), uv, 0.0).r;
}

vec4 GetOrthoSurfacePosition(vec2 screenUV) {
    vec3 viewPosition = reconstructPosition(screenUV);
    vec3 relativeWorldPosition = (vec4(viewPosition, 0.0) * c.invView).xyz;
    return vec4(relativeWorldPosition, 1.0);
}

vec2 RoofHash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec2 RoofWander(vec2 worldXY) {
    vec2 p = worldXY * 0.08;
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 a = RoofHash22(i);
    vec2 b = RoofHash22(i + vec2(1.0, 0.0));
    vec2 c0 = RoofHash22(i + vec2(0.0, 1.0));
    vec2 d = RoofHash22(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c0, d, u.x), u.y) - 0.5;
}

float SampleOrthoDepthPoint(vec2 uv) {
    return textureLod(sampler2D(t_ortho, NVR_SAMPLER(pc.sPoint)), uv, 0.0).r;
}

float GetRoofMask(vec4 orthoPos, vec2 worldXY) {
    orthoPos.xyz /= orthoPos.w;

    float outofbounds = float(orthoPos.x < -1.0 || orthoPos.x > 1.0 ||
        orthoPos.y < -1.0 || orthoPos.y > 1.0 ||
        orthoPos.z < 0.0 || orthoPos.z > 1.0);

    float texel = c.orthoData.y > 0.0 ? c.orthoData.y : (1.0 / 512.0);
    float res = 1.0 / texel;

    vec2 uv = vec2(orthoPos.x * 0.5 + 0.5, orthoPos.y * -0.5 + 0.5);
    uv += RoofWander(worldXY) * (0.9 * texel);

    vec2 p = uv * res - 0.5;
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 c00 = (i + 0.5) * texel;
    float zRef = orthoPos.z - 0.0005;

    float s00 = SampleOrthoDepthPoint(c00) > zRef ? 1.0 : 0.0;
    float s10 = SampleOrthoDepthPoint(c00 + vec2(texel, 0.0)) > zRef ? 1.0 : 0.0;
    float s01 = SampleOrthoDepthPoint(c00 + vec2(0.0, texel)) > zRef ? 1.0 : 0.0;
    float s11 = SampleOrthoDepthPoint(c00 + vec2(texel, texel)) > zRef ? 1.0 : 0.0;

    float lit = mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
    return smoothstep(0.0, 1.0, lit) * (1.0 - outofbounds);
}

float GetOrthoDepthScale() {
    vec3 depthAxis = c.shadowToLight[2].xyz;
    return max(length(depthAxis) / max(abs(c.shadowToLight[3][3]), 0.000001), 0.00000001);
}

vec2 GetCurvatureSampleRadius() {
    vec3 axisX = c.shadowToLight[0].xyz;
    vec3 axisY = c.shadowToLight[1].xyz;
    float invW = 1.0 / max(abs(c.shadowToLight[3][3]), 0.000001);
    return max(vec2(length(axisX), length(axisY)) * (0.5 * CurvatureWorldRadius * invW), vec2(0.000001));
}

float GetViewModelMask(vec2 screenUV) {
    float vmDepthX = texture(sampler2D(t_depthVM, NVR_SAMPLER(pc.sLin)), screenUV).x;
    return ((c.depthConstants.z == 0.0 && vmDepthX < 0.9) ||
        (c.depthConstants.z > 0.0 && vmDepthX > 0.01)) ? 1.0 : 0.0;
}

float GetPuddleViewModelExclusion(vec2 ruv, float centerMask) {
    vec2 pixel = c.reciprocalResolution.xy;
    float mask = centerMask;
    mask = max(mask, GetViewModelMask(clampToRect(ruv + vec2(pixel.x, 0.0), pixel)));
    mask = max(mask, GetViewModelMask(clampToRect(ruv - vec2(pixel.x, 0.0), pixel)));
    mask = max(mask, GetViewModelMask(clampToRect(ruv + vec2(0.0, pixel.y), pixel)));
    mask = max(mask, GetViewModelMask(clampToRect(ruv - vec2(0.0, pixel.y), pixel)));
    return mask;
}

float ComputeDepressionMask(vec4 relativeSurface, float viewDepth, float vmMask) {
    vec4 mapPos = relativeSurface * c.shadowToLight;
    mapPos.xyz /= mapPos.w;
    mapPos.x = mapPos.x * 0.5 + 0.5;
    mapPos.y = mapPos.y * -0.5 + 0.5;
    vec2 uv = mapPos.xy;
    vec2 radius = GetCurvatureSampleRadius();
    float valid = step(radius.x, uv.x) * step(uv.x, 1.0 - radius.x)
        * step(radius.y, uv.y) * step(uv.y, 1.0 - radius.y)
        * step(0.0, mapPos.z) * step(mapPos.z, 1.0);

    float cd = SampleOrthoDepth(uv);
    float left = SampleOrthoDepth(uv + vec2(-radius.x, 0.0));
    float right = SampleOrthoDepth(uv + vec2(radius.x, 0.0));
    float top = SampleOrthoDepth(uv + vec2(0.0, -radius.y));
    float bottom = SampleOrthoDepth(uv + vec2(0.0, radius.y));
    vec2 diagonalRadius = radius * 0.70710678;
    float d1 = SampleOrthoDepth(uv - diagonalRadius);
    float d2 = SampleOrthoDepth(uv + diagonalRadius);
    float d3 = SampleOrthoDepth(uv + vec2(-diagonalRadius.x, diagonalRadius.y));
    float d4 = SampleOrthoDepth(uv + vec2(diagonalRadius.x, -diagonalRadius.y));
    float depthScale = GetOrthoDepthScale();
    float meanRing = (left + right + top + bottom + d1 + d2 + d3 + d4) * 0.125;
    float signedCurve = (cd - meanRing) * CurvatureDepthSign / depthScale;
    float concave = smoothstep(DepressionMinCurvature, DepressionFullCurvature, signedCurve);

    vec2 depthGradient = vec2(right - left, bottom - top) / (2.0 * radius);
    vec3 clipGradient = vec3(-0.5 * depthGradient.x, 0.5 * depthGradient.y, 1.0);
    vec3 heightNormal = mat3(c.shadowToLight) * clipGradient;
    heightNormal /= max(length(heightNormal), 0.000001);
    float slopeFilter = smoothstep(DepressionSlopeCutoff, DepressionSlopeFadeStart, abs(heightNormal.z));

    float depthJump = max(max(abs(left - cd), abs(right - cd)),
        max(abs(top - cd), abs(bottom - cd))) / depthScale;
    float edgeFilter = 1.0 - smoothstep(DepressionMaxDepthJump * 0.5, DepressionMaxDepthJump, depthJump);
    concave *= slopeFilter;
    concave *= edgeFilter;

    float surfaceMatch = 1.0 - smoothstep(CurvatureSurfaceTolerance,
        CurvatureSurfaceTolerance * 2.0, abs(mapPos.z - cd));
    valid *= surfaceMatch * step(0.0, viewDepth);
    concave *= valid * (1.0 - vmMask);
    return concave;
}
