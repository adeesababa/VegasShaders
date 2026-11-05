#include <algorithm>

#include "Skin.h"

void SkinShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinMaterial", &Constants.Material);
	TheShaderManager->RegisterConstant("TESR_SkinLook", &Constants.Look);
	TheShaderManager->RegisterConstant("TESR_SkinDebug", &Constants.Debug);
}

// The material in use (outdoors or in), each frame.
void SkinShaders::UpdateConstants() {
	const bool isExterior = TheShaderManager->GameState.isExterior;
	const MaterialStruct& Material = isExterior ? Exterior : Interior;
	Constants.Material = D3DXVECTOR4(Material.SpecularStrength, Material.Roughness, Material.Scattering, Material.Translucency);

	// Rain wets skin: smoother and shinier. The same rain / puddle factor the object and terrain shaders use.
	float wetness = 0.0f;
	if (isExterior && TheShaderManager->Effects.WetWorld) {
		const D3DXVECTOR4& rain = TheShaderManager->Effects.WetWorld->Constants.Data;
		wetness = std::clamp((std::max)(rain.x, rain.z) * RainWetness, 0.0f, 1.0f);
	}
	// Nothing occludes the sky the reflection samples, so indoors it would light skin from a sky it cannot see.
	Constants.Look = D3DXVECTOR4(isExterior ? SkyReflection : 0.0f, wetness, Material.ShadowScatter, VanillaMatchedHighlights);

	// The shader's own views only; the blur's views leave the skin drawn normally.
	// z: [Shaders.Skin.Debug] LegacyFaceShadows (the shadow reach along the ray off, live, for an A/B: SkinTemplate.hlsl SHADOW_REACH_ALONG_RAY).
	Constants.Debug = D3DXVECTOR4(DebugView < FirstScatteringDebugView ? (float)DebugView : 0.0f, isExterior ? 0.0f : 1.0f, LegacyFaceShadows ? 1.0f : 0.0f, 0.0f);
}

static void ReadSkinMaterial(SkinShaders::MaterialStruct* Material, const char* Section) {
	Material->SpecularStrength = std::clamp(TheSettingManager->GetSettingF(Section, "SpecularStrength"), 0.0f, 4.0f);
	Material->Roughness = std::clamp(TheSettingManager->GetSettingF(Section, "Roughness"), 0.1f, 1.0f);
	Material->Scattering = std::clamp(TheSettingManager->GetSettingF(Section, "Scattering"), 0.0f, 4.0f);
	Material->Translucency = std::clamp(TheSettingManager->GetSettingF(Section, "Translucency"), 0.0f, 2.0f);
	Material->ShadowScatter = std::clamp(TheSettingManager->GetSettingF(Section, "ShadowScatter"), 0.0f, 2.0f);
}

void SkinShaders::UpdateSettings() {
	ReadSkinMaterial(&Exterior, "Shaders.Skin.Main");
	ReadSkinMaterial(&Interior, "Shaders.Skin.Interiors");
	SkyReflection = std::clamp(TheSettingManager->GetSettingF("Shaders.Skin.Main", "SkyReflection"), 0.0f, 2.0f);
	RainWetness = std::clamp(TheSettingManager->GetSettingF("Shaders.Skin.Main", "RainWetness"), 0.0f, 1.0f);
	VanillaMatchedHighlights = std::clamp(TheSettingManager->GetSettingF("Shaders.Skin.Main", "VanillaMatchedHighlights"), 0.0f, 1.0f);	// UNOFFICIAL 2026-11-05 (PB NVR's skin lighting)
	MuteGameHighlights = TheSettingManager->GetSettingI("Shaders.Skin.Main", "MuteGameHighlights") != 0;
	DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Skin.Debug", "DebugView"), 0, DebugViewCount);
	BindFaceGenMaps = TheSettingManager->GetSettingI("Shaders.Skin.Debug", "BindFaceGenMaps") != 0;
	LegacyFaceShadows = TheSettingManager->GetSettingI("Shaders.Skin.Debug", "LegacyFaceShadows") != 0;
}
