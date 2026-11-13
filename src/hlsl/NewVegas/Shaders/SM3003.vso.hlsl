// Hair vertex shader -- port of vanilla SM3003.vso, plus the camera-relative world position
// the forward shadow lookup needs.
// This is the hair pass: traced as
//     Pass 400 BSSM_3XLIGHTING_HSpc, FaceGenHairNoHat (SM3003.vso SM3003.pso)
// Note it is the SM3 family, not HAIR*.vso -- those are used by a different, unlit pass.
// Translated instruction-for-instruction from the vanilla vs_3_0 disassembly. Everything is
// passed through in OBJECT space; the pixel shader builds its own TBN from TEXCOORD3/4/5 and
// uses TEXCOORD6 as the position. The only addition is shadowWorldPos on TEXCOORD1, which
// vanilla leaves unwritten.
// Vanilla registers: ModelViewProj c0-c3, FogParam c44, FogColor c45. No relative addressing,
// so Shadow.hlsl's default c100/c104 pins are clear.
// UNOFFICIAL: the camera matrices Shadow.hlsl rebuilds world positions with move out of c100-c107 in skinned vertex
// shaders: the game's bone upload reaches past Bones[54] (c97) and overwrote them, so actors sampled the sun shadow
// at the wrong place. c240-c247 is beyond any bone write.
#define SHADOW_INVPROJ_REG c240
#define SHADOW_INVVIEW_REG c244
#include "includes/Shadow.hlsl"
// UNOFFICIAL 2026-11-12 HairAndEyes (Includes/FlashBeam.hlsl): the vector to the flashlight in object space for the hair's pixel
// shaders (TEXCOORD2, which the game's shaders leave unused). c10-c14: free here (c0-c3, c44, c45 and c240-c247 are taken).
#define FLASH_BEAM_REG c10
#include "includes/FlashBeam.hlsl"

uniform row_major float4x4 ModelViewProj : register(c0);
uniform float4 FogParam : register(c44);
uniform float4 FogColor : register(c45);

struct VtxAttrs {
    float4 f_position : POSITION;
    float3 f_tangent  : TANGENT;
    float3 f_binormal : BINORMAL;
    float3 f_normal   : NORMAL;
    float2 f_uv       : TEXCOORD0;
    float4 f_color    : COLOR0;
};

struct VtxVaryings {
    float4 f_position       : POSITION;
    float2 f_uv             : TEXCOORD0;
    float4 f_shadow_world_pos : TEXCOORD1;   // the only addition to vanilla
    float4 f_beam_obj       : TEXCOORD2;   // UNOFFICIAL 2026-11-12 xyz from the vertex to the flashlight, object space (a direction)
    float4 f_color          : COLOR0;
    float3 f_tangent        : TEXCOORD3;   // object space, unnormalised, as vanilla passes them
    float3 f_binormal       : TEXCOORD4;
    float3 f_normal         : TEXCOORD5;
    float3 f_obj_pos         : TEXCOORD6;
    float4 f_fog            : TEXCOORD7;   // rgb: fog colour, w: fog amount
};

VtxVaryings main(VtxAttrs va) {
    VtxVaryings vr;

    vr.f_position = mul(ModelViewProj, va.f_position);

    // Fog, from the clip-space xyz distance.
    float fog_dist = length(vr.f_position.xyz);
    float fog_strength = saturate((FogParam.x - fog_dist) / FogParam.y);
    vr.f_fog.rgb = FogColor.rgb;
    vr.f_fog.w = pow(1.0f - fog_strength, FogParam.z);

    vr.f_uv = va.f_uv;
    vr.f_color = va.f_color;
    vr.f_tangent = va.f_tangent;
    vr.f_binormal = va.f_binormal;
    vr.f_normal = va.f_normal;
    vr.f_obj_pos = va.f_position.xyz;

    vr.f_shadow_world_pos = float4(GetShadowWorldPos(vr.f_position), SHADOW_VS_SENTINEL);
    // UNOFFICIAL 2026-11-12 HairAndEyes: from the vertex to the flashlight, in the object's axes (the pixel shader lights in them)
    vr.f_beam_obj = float4(FlashBeamToObject(TESR_FlashBeam[0].xyz - vr.f_shadow_world_pos.xyz, ModelViewProj), 0.0f);

    return vr;
};
