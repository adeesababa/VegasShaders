#pragma once
// UNOFFICIAL 2026-11-04 (user: "lets fade the way lights interact with envmap_light_fade at night"). The env-map shine of assets with the
// BSS2_Envmap_Light_Fade flag follows the light round them: ShadowLightShader::SetupGeometryConstants_AmbientEmittance multiplies the env
// pass's c1.w by F = max(the sun's luminance, the first active non-shadow lamp's luminance x (1 - d / r)), d from the lamp to the OBJECT's
// bound centre. At night (no sun) a lamp makes a whole object gleam at one strength, which jumps as the lamp or the player moves.
// Per env pass (0x244-0x24B, before the game sets it up; Render.cpp ForwardShadowsForPass) the same F is worked out here with the same lamp
// (the game's GetFirstActiveNonShadowLight) and sent with the lamp to EnvTemplate.hlsl (c28-c31), which shades per pixel instead:
// P = max(sun, lamp x (1 - (d/r)^2)^2 at the pixel, times the beam's cone for the flashlight's game lamp), the shine scaled by P / F.
// After the draw c28 goes back to 0 (AfterPass): no other env draw takes it.
#include <cmath>
#include <algorithm>
#include <d3dx9.h>
#include "FlashlightLamp.h"

namespace EnvLightFade {
	inline bool Set = false;
	inline unsigned long long StatPasses = 0;

	inline float Luminance(const NiColor& c) { return c.g * 0.36f + c.r * 0.31f + c.b * 0.32f; }	// NiColor::Luminance 0xB719E0

	inline void ForPass(const void* pass) {
		if (!pass) return;
		const UInt8* p = (const UInt8*)pass;
		const UInt16 type = *(const UInt16*)(p + 4);
		if (type < 0x244 || type > 0x24B) return;
		NiGeometry* geometry = *(NiGeometry* const*)p;
		if (!geometry) return;
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		if (!property || !property->IsLightingProperty()) return;
		if (!(property->ulFlags[1] & 0x8000) || (property->ulFlags[0] & 0x20000)) return;	// BSS2_Envmap_Light_Fade, not BSS1_Eye_Environment_Mapping

		void* iter = nullptr;
		ShadowSceneLight* lamp = ThisStdCall<ShadowSceneLight*>(0xB70600, property, &iter);	// BSShaderLightingProperty::GetFirstActiveNonShadowLight
		if (!lamp || !lamp->sourceLight) return;	// the sun alone: one value is right
		const NiPointLight* light = lamp->sourceLight;
		const float radius = light->Spec.r;
		if (!(radius > 1.0f)) return;

		float sunLum = 0.0f;
		if (ShadowSceneNode* node = BSShaderManager::GetShadowSceneNode(0))
			if (node->directionalLight && node->directionalLight->sourceLight) sunLum = Luminance(node->directionalLight->sourceLight->Diff);
		const NiBound* bound = geometry->GetWorldBound();
		const float dx = light->m_worldTransform.pos.x - bound->Center.x, dy = light->m_worldTransform.pos.y - bound->Center.y, dz = light->m_worldTransform.pos.z - bound->Center.z;
		const float lampLum = Luminance(light->Diff) * light->Dimmer;
		const float gameF = (std::max)(sunLum, lampLum * (1.0f - (std::min)((std::max)(sqrtf(dx * dx + dy * dy + dz * dz) / radius, 0.0f), 1.0f)));

		const bool flash = FlashlightLamp::Is(light);
		const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
		// the flashlight's lamp: its full brightness (its dimmer here is already its cone at the object, FlashlightLamp::ForPass)
		const float pixelLum = flash ? Luminance(light->Diff) * FlashlightLamp::Dimmer : lampLum;
		const float c[4][4] = {
			{ -1.0f, gameF, sunLum, pixelLum },	// (x -1: on; FlashlightLamp puts its slot + 1, positive, in c28.x for the object shaders)
			{ light->m_worldTransform.pos.x - cam.x, light->m_worldTransform.pos.y - cam.y, light->m_worldTransform.pos.z - cam.z, radius },
			{ FlashlightLamp::Axis.x, FlashlightLamp::Axis.y, FlashlightLamp::Axis.z, FlashlightLamp::CosOuter },
			{ FlashlightLamp::CosInner, flash ? 1.0f : 0.0f, 0.0f, 0.0f } };
		TheRenderManager->device->SetPixelShaderConstantF(28, &c[0][0], 4);
		Set = true;
		StatPasses++;
		if (StatPasses <= 3 || StatPasses % 50000 == 0)
			Logger::Log("UNOFFICIAL EnvLightFade: env pass %llu shaped per pixel (0x%X '%s': the game's fade %.3f, sun %.3f, lamp %.3f%s)", StatPasses, (unsigned)type,
				geometry->m_pcName ? geometry->m_pcName : "?", gameF, sunLum, pixelLum, flash ? ", the flashlight's" : "");
	}
	inline void AfterPass() {
		if (!Set) return;
		static const float none[4] = {};
		TheRenderManager->device->SetPixelShaderConstantF(28, none, 1);
		Set = false;
	}
}
