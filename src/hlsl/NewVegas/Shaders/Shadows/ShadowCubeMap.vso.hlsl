// UNOFFICIAL cloth wind (../includes/ClothWind.hlsl): cloth's shadow moves with it in lamp cube maps too (position only).
#ifndef CLOTH_WIND_ON
    #define CLOTH_WIND_ON 0
#endif
#if CLOTH_WIND_ON
    #include "../includes/ClothWind.hlsl"
#endif

uniform row_major float4x4 TESR_ShadowWorldTransform : register(c0);
uniform row_major float4x4 TESR_ShadowViewProjTransform : register(c4);
uniform float4 TESR_ShadowData : register(c8);
uniform float4 Bones[120] : register(c9);	// UNOFFICIAL 2026-11-04 up to 40 bones (was 54 rows, 18: see ShadowMap.vso)
uniform float4 TESR_ShadowCubeMapLightPosition : register(c129);	// (set by name)

#define	weight(v)		dot(v, 1)

#if CLOTH_WIND_ON
float3 ClothBonesVector(float4 ofs, float3 bw_v, float3 vv) {
	const float4 vw_v = float4(vv, 0);
	const float3 tmp_v8 = mul(float3x4(Bones[0 + ofs.w], Bones[1 + ofs.w], Bones[2 + ofs.w]), vw_v);
	const float3 tmp_v6 = mul(float3x4(Bones[0 + ofs.z], Bones[1 + ofs.z], Bones[2 + ofs.z]), vw_v);
	const float3 tmp_v5 = mul(float3x4(Bones[0 + ofs.x], Bones[1 + ofs.x], Bones[2 + ofs.x]), vw_v);
	const float3 tmp_v4 = mul(float3x4(Bones[0 + ofs.y], Bones[1 + ofs.y], Bones[2 + ofs.y]), vw_v);
	return ((1 - weight(bw_v)) * tmp_v8) + (bw_v.z * tmp_v6) + (bw_v.x * tmp_v5) + (tmp_v4 * bw_v.y);
}
#endif

struct VtxAttrs {
    float4 f_position : POSITION;
	float4 f_texcoord_0 : TEXCOORD0;
    float4 f_blendweight : BLENDWEIGHT;
    float4 f_blendindexes : BLENDINDICES;
#if CLOTH_WIND_ON
    float4 f_color : COLOR0;
    float3 f_normal : NORMAL;
    float3 f_cloth_weight : TEXCOORD7;
#endif
};

struct VtxVaryings {
    float4 f_position : POSITION;
    float4 f_texcoord_0 : TEXCOORD0;
	float4 f_texcoord_1 : TEXCOORD1;
};

VtxVaryings main(VtxAttrs va) {
    VtxVaryings vr;

	const float4 loc_const_0 = {1.0f, 765.01001f, 0.0f, 0.0f};
	
	float4 ofs;
    float4 tmp_q0;
    float4 tmp_q4;
    float4 tmp_q5;
    float4 tmp_q6;
    float4 tmp_q7;
    float4 tmp_q8;
	float4 tmp_r1 = 0.0f;
	float4 tmp_r0 = va.f_position;
#if CLOTH_WIND_ON
	float3 cloth_normal = va.f_normal;
	float3 cloth_out = 0.0f;
	float4 cloth_leg_l = 0.0f, cloth_leg_r = 0.0f;
	float3 cloth_up = 0.0f;
#endif
	
	if (TESR_ShadowData.x == 1.0f) { // Skinned (Actors)
		ofs = va.f_blendindexes.zyxw * loc_const_0.y;
		tmp_r0.w = 1;
		tmp_q0 = (va.f_position.xyzx * loc_const_0.xxxz) + loc_const_0.zzzx;
		tmp_q8.xyz = mul(float3x4(Bones[0 + ofs.w], Bones[1 + ofs.w], Bones[2 + ofs.w]), tmp_q0);
		tmp_q6.xyz = mul(float3x4(Bones[0 + ofs.z], Bones[1 + ofs.z], Bones[2 + ofs.z]), tmp_q0);
		tmp_q5.xyz = mul(float3x4(Bones[0 + ofs.x], Bones[1 + ofs.x], Bones[2 + ofs.x]), tmp_q0);
		tmp_q4.xyz = mul(float3x4(Bones[0 + ofs.y], Bones[1 + ofs.y], Bones[2 + ofs.y]), tmp_q0);
		tmp_q7.xyz = (va.f_blendweight.z * tmp_q6.xyz) + ((va.f_blendweight.x * tmp_q5.xyz) + (tmp_q4.xyz * va.f_blendweight.y));
		tmp_r0.xyz = ((1 - weight(va.f_blendweight.xyz)) * tmp_q8.xyz) + tmp_q7.xyz;
#if CLOTH_WIND_ON
		{
			const float4 nw_v = float4(va.f_normal, 0);
			const float3 tmp_n8 = mul(float3x4(Bones[0 + ofs.w], Bones[1 + ofs.w], Bones[2 + ofs.w]), nw_v);
			const float3 tmp_n6 = mul(float3x4(Bones[0 + ofs.z], Bones[1 + ofs.z], Bones[2 + ofs.z]), nw_v);
			const float3 tmp_n5 = mul(float3x4(Bones[0 + ofs.x], Bones[1 + ofs.x], Bones[2 + ofs.x]), nw_v);
			const float3 tmp_n4 = mul(float3x4(Bones[0 + ofs.y], Bones[1 + ofs.y], Bones[2 + ofs.y]), nw_v);
			cloth_normal = ((1 - weight(va.f_blendweight.xyz)) * tmp_n8) + (va.f_blendweight.z * tmp_n6) + (va.f_blendweight.x * tmp_n5) + (tmp_n4 * va.f_blendweight.y);
		}
		[branch] if (ClothIsMask5() && ClothWalk.x > 0.5f) {
			tmp_r0.xyz = lerp(tmp_r0.xyz, ClothPelvisPoint(va.f_position.xyz), saturate(va.f_cloth_weight.x) * ClothWalk.y);
			cloth_out = ClothPelvisVector(ClothOutwardBind(va.f_position.xyz));
			cloth_up = ClothPelvisVector(float3(0.0f, 0.0f, 1.0f));
			cloth_leg_l = ClothLegBind(tmp_r0.xyz, ClothLegNow[0], ClothLegNow[1], ClothLegNow[2]);
			cloth_leg_r = ClothLegBind(tmp_r0.xyz, ClothLegNow[3], ClothLegNow[4], ClothLegNow[5]);
		}
		else [branch] if (ClothIsMask5()) {
			const float4 ow_v = float4(ClothOutwardBind(va.f_position.xyz), 0);
			const float3 tmp_o8 = mul(float3x4(Bones[0 + ofs.w], Bones[1 + ofs.w], Bones[2 + ofs.w]), ow_v);
			const float3 tmp_o6 = mul(float3x4(Bones[0 + ofs.z], Bones[1 + ofs.z], Bones[2 + ofs.z]), ow_v);
			const float3 tmp_o5 = mul(float3x4(Bones[0 + ofs.x], Bones[1 + ofs.x], Bones[2 + ofs.x]), ow_v);
			const float3 tmp_o4 = mul(float3x4(Bones[0 + ofs.y], Bones[1 + ofs.y], Bones[2 + ofs.y]), ow_v);
			cloth_out = ((1 - weight(va.f_blendweight.xyz)) * tmp_o8) + (va.f_blendweight.z * tmp_o6) + (va.f_blendweight.x * tmp_o5) + (tmp_o4 * va.f_blendweight.y);
			const float4 leg_l = ClothLegBind(va.f_position.xyz, ClothLimb[0], ClothLimb[1], ClothLimb[2]);
			const float4 leg_r = ClothLegBind(va.f_position.xyz, ClothLimb[4], ClothLimb[5], ClothLimb[6]);
			cloth_leg_l = float4(ClothBonesVector(ofs, va.f_blendweight.xyz, leg_l.xyz), leg_l.w);
			cloth_leg_r = float4(ClothBonesVector(ofs, va.f_blendweight.xyz, leg_r.xyz), leg_r.w);
			cloth_up = ClothBonesVector(ofs, va.f_blendweight.xyz, float3(0.0f, 0.0f, 1.0f));
		}
#endif
	}
#if CLOTH_WIND_ON
	if (TESR_ShadowData.x == 0.0f) cloth_out = float3(va.f_cloth_weight.y, 0.0f, 0.0f);
	if (TESR_ShadowData.x == 0.0f || TESR_ShadowData.x == 1.0f) tmp_r0.xyz = ClothWindPosition(tmp_r0.xyz, cloth_normal, va.f_color, va.f_position.xyz, va.f_cloth_weight.x, cloth_out, cloth_leg_l, cloth_leg_r, cloth_up);
	if (TESR_ShadowData.x == 1.0f && ClothIsMask5() && ClothWalk.x > 0.5f) {
		tmp_r0.xyz += ClothRingAt(va.f_cloth_weight.y) * va.f_cloth_weight.z;
		tmp_r0.xyz += ClothLegPush(tmp_r0.xyz, cloth_out);
	}
#endif
    if (TESR_ShadowData.x != 1.0f) tmp_r0 = mul(tmp_r0, TESR_ShadowWorldTransform);
	tmp_r1.xyz = TESR_ShadowCubeMapLightPosition.xyz - tmp_r0.xyz;
	tmp_r0 = mul(tmp_r0, TESR_ShadowViewProjTransform);
	vr.f_position = tmp_r0;
    vr.f_texcoord_0 = tmp_r1;
	vr.f_texcoord_1 = va.f_texcoord_0;
    return vr;
	
};