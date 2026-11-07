// UNOFFICIAL 2026-11-07 (protocol 7) real textures in the RT helper (tools/nvr-rt-helper/NvrRtTextures.inl, docs/next/RT-REAL-TEXTURES.md):
// the game's own textures, full size with every mip, sampled with filtering and a mip by the ray's footprint, where the atlases kept a
// 64-256 texel copy read texel by texel (blocky up close, speckled far away). Included by RtHit.glsl and RtAlpha.glsl after the records
// (binding 10) and the geometry data (binding 11) are declared. Descriptor set 1 (the helper binds it to every pass):
//   binding 0: per atlas slot (the record's (flags >> 16) & 0x3FF) the diffuse / normal / glow texture's index (0 = none: the atlas)
//   binding 1: the sampler (linear, mips, repeat, anisotropic); binding 2: the textures
// rtConeWidth: the width of the ray's footprint at the hit in world units (set it before calling rtHit*: the camera's pixel spread times
// the distance travelled, widened by rough reflections). 0 (the default) = the finest mip (exact alpha tests, shadow rays).
// A shader built before protocol 7 never declares set 1 and keeps the atlases; RT_NO_REAL_TEXTURES leaves this out on purpose.

#ifndef RT_TEXTURES_INCLUDED
#define RT_TEXTURES_INCLUDED
#ifndef RT_NO_REAL_TEXTURES
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0, std430) readonly buffer RtTexTable { uvec4 rtTexSlots[]; };
layout(set = 1, binding = 1) uniform sampler rtTexSampler;
layout(set = 1, binding = 2) uniform texture2D rtTextures[4096];

float rtConeWidth = 0.0;

// The slot's real textures: x diffuse, y normal map, z glow map (0 = none)
uvec3 rtSlotTextures(uint flags) { return rtTexSlots[(flags >> 16) & 0x3FFu].xyz; }

// The mip for texture `id` at a hit on (instance, primitive): texels per world unit of the triangle (its texcoord area against its
// object-space area) times the footprint. Object space: the instance's scale is left out (references are nearly all at scale 1).
float rtTexLod(uint id, uint instance, uint primitive) {
	if (rtConeWidth <= 0.0 || records[instance].vertexOffset == 0xFFFFFFFFu) return 0.0;
	const uint io = records[instance].indexOffset + 3u * primitive;
	const uvec3 i = uvec3(words[io], words[io + 1u], words[io + 2u]);
	const uint vo = records[instance].vertexOffset, uo = floatBitsToUint(records[instance].albedo.w);
	const vec3 p0 = vec3(uintBitsToFloat(words[vo + 3u * i.x]), uintBitsToFloat(words[vo + 3u * i.x + 1u]), uintBitsToFloat(words[vo + 3u * i.x + 2u]));
	const vec3 p1 = vec3(uintBitsToFloat(words[vo + 3u * i.y]), uintBitsToFloat(words[vo + 3u * i.y + 1u]), uintBitsToFloat(words[vo + 3u * i.y + 2u]));
	const vec3 p2 = vec3(uintBitsToFloat(words[vo + 3u * i.z]), uintBitsToFloat(words[vo + 3u * i.z + 1u]), uintBitsToFloat(words[vo + 3u * i.z + 2u]));
	const vec2 u0 = vec2(uintBitsToFloat(words[uo + 2u * i.x]), uintBitsToFloat(words[uo + 2u * i.x + 1u]));
	const vec2 u1 = vec2(uintBitsToFloat(words[uo + 2u * i.y]), uintBitsToFloat(words[uo + 2u * i.y + 1u]));
	const vec2 u2 = vec2(uintBitsToFloat(words[uo + 2u * i.z]), uintBitsToFloat(words[uo + 2u * i.z + 1u]));
	const float worldArea = length(cross(p1 - p0, p2 - p0));
	const float uvArea = abs((u1.x - u0.x) * (u2.y - u0.y) - (u2.x - u0.x) * (u1.y - u0.y));
	const vec2 size = vec2(textureSize(sampler2D(rtTextures[nonuniformEXT(id)], rtTexSampler), 0));
	const float texelsPerUnit = sqrt(uvArea * size.x * size.y / max(worldArea, 1.0e-8));
	return log2(max(rtConeWidth * texelsPerUnit, 1.0e-6));
}

vec4 rtTexSample(uint id, vec2 uv, float lod) {
	return textureLod(sampler2D(rtTextures[nonuniformEXT(id)], rtTexSampler), uv, lod);
}

vec3 rtTexToLinear(vec3 c) {   // the textures are sRGB-encoded (sampled as UNORM, as the atlases)
	return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}
#endif
#endif
