#include <algorithm>

#include "../../core/DxvkCompute.h"
#include "WetnessReflections.h"
#include "../../core/FdIndirect.h"	// UNOFFICIAL 2026-11-18 DeferredBumpedNormals

void WetnessReflectionsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WetnessReflectionData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_WetnessReflectionFade", &Constants.Fade);
	TheShaderManager->RegisterConstant("TESR_WetnessReflectionBump", &Constants.Bump);	// UNOFFICIAL 2026-11-18 DeferredBumpedNormals
}

// Part of Wetness Effects: [Shaders.WetnessEffects.Reflections], switched on there. It has no Status of its
// own, so the effect always loads and ShouldRender decides.
void WetnessReflectionsEffect::UpdateSettings() {
	const char* main = "Shaders.WetnessEffects.Reflections";
	Settings.Enabled = TheSettingManager->GetSettingI(main, "Enabled") != 0;
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF(main, "Strength"), 0.0f, 2.0f);
	Settings.MaxDistance = std::clamp(TheSettingManager->GetSettingF(main, "MaxDistance"), 100.0f, 10000.0f);
	Settings.Thickness = std::clamp(TheSettingManager->GetSettingF(main, "Thickness"), 1.0f, 500.0f);
	Settings.Steps = (float)std::clamp(TheSettingManager->GetSettingI(main, "Steps"), 4, 64);
	Settings.RoughnessCutoff = std::clamp(TheSettingManager->GetSettingF(main, "RoughnessCutoff"), 0.05f, 1.0f);
	Settings.EdgeFade = std::clamp(TheSettingManager->GetSettingF(main, "EdgeFade"), 0.01f, 0.5f);
	Settings.MaxCameraDistance = std::clamp(TheSettingManager->GetSettingF(main, "MaxCameraDistance"), 500.0f, 50000.0f);
	Settings.DebugView = TheSettingManager->GetSettingI(main, "DebugView") != 0;
}

void WetnessReflectionsEffect::UpdateConstants() {
	Constants.Data = D3DXVECTOR4(Settings.Strength, Settings.MaxDistance, Settings.Thickness, Settings.Steps);
	Constants.Fade = D3DXVECTOR4(Settings.RoughnessCutoff, Settings.EdgeFade, Settings.DebugView ? 1.0f : 0.0f, Settings.MaxCameraDistance);
	Constants.Bump.y = (float)TheSettingManager->GetSettingI("Shaders.PBR.Main", "SkylightingMode");
}

// Yields to Reflections when Reflections is active (Reflections owns wet-film & puddle reflections across SSR, Vulkan and RT + denoiser).
// Falls back to this standalone pass only when the user explicitly disables Reflections. Supports both SkylightingMode 0 and 1.
bool WetnessReflectionsEffect::ShouldRender() {
	if (!Settings.Enabled) return false;
	if (!TheShaderManager->GameState.isExterior || TheShaderManager->GameState.isUnderwater) return false;
	WetnessEffectsShaders* Wetness = TheShaderManager->Shaders.Wetness;
	if (!Wetness || !Wetness->Enabled) return false;
	if (TheShaderManager->Effects.Reflections && TheShaderManager->Effects.Reflections->Enabled &&
		TheShaderManager->Effects.Reflections->IsLoaded() && TheShaderManager->Effects.Reflections->ShouldRender())
		return false;
	if (Settings.DebugView) return true;
	return Wetness->Constants.Flags.x > 0.5f && Settings.Strength > 0.0f;
}

// UNOFFICIAL 2026-11-18 [Main.Main.Quality] DeferredBumpedNormals (live, test only, off; core/FdIndirect.h, docs/bumped-normals-study.md):
// the shader's NVR_WetShadingNormals (s4) has no texture slot, so it is set here before every Render (EffectChain): the G-buffer's
// normal-mapped shading normal while the switch is on, else TESR_NormalsBuffer itself; TESR_WetnessReflectionBump.x says which (0: the
// shader takes exactly its old path). The caller clears the stage after the Render (ClearPassTextures).
void WetnessReflectionsEffect::BindShadingNormals(IDirect3DDevice9* Device) {
	Constants.Bump.x = 0.0f;
	shadingNormals = nullptr;
	if (!Enabled || !Effect || !ShouldRender()) return;	// Render draws nothing: no shading-normals pass for it either
	std::string normalsName = "TESR_NormalsBuffer", depthName = "TESR_DepthBuffer";
	IDirect3DBaseTexture9** normalsSlot = TheTextureManager->GetTextureSlotByName(normalsName);
	IDirect3DBaseTexture9* normals = normalsSlot ? *normalsSlot : nullptr;
	if (!normals) return;
	IDirect3DBaseTexture9* shading = normals;
	if (FdIndirect::BumpedNormalsOn()) {
		IDirect3DBaseTexture9** depth = TheTextureManager->GetTextureSlotByName(depthName);
		shading = FdIndirect::ShadingNormals(Device, normals, depth ? *depth : nullptr);
	}
	Constants.Bump.x = shading != normals ? 1.0f : 0.0f;
	shadingNormals = shading;
	SetPassTexture(4, shading);	// set after each pass begins (EffectRecord::PassTextures)
}

bool WetnessReflectionsEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0 || ClearRenderTarget || !DxvkCompute::Available()) return false;
	auto findTex = [](const char* name) -> IDirect3DBaseTexture9* {
		auto it = TheTextureManager->TextureNames.find(name);
		return (it != TheTextureManager->TextureNames.end() && it->second) ? *it->second : nullptr;
	};
	IDirect3DBaseTexture9* rendered = TheTextureManager->RenderedTexture;
	IDirect3DBaseTexture9* depth = findTex("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = findTex("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* ortho = findTex("TESR_OrthoMapBuffer");
	IDirect3DBaseTexture9* depthVM = findTex("TESR_DepthBufferViewModel");
	if (!rendered || !depth || !normals || !depthVM) return false;
	if (!ortho) ortho = depth;
	IDirect3DBaseTexture9* shading = shadingNormals ? shadingNormals : normals;

	struct WetReflUniforms {
		float projectionTransform[16];
		float invProjectionTransform[16];
		float invViewTransform[16];
		float shadowOrtho[16];
		float reciprocalResolution[4];
		float depthConstants[4];
		float cameraData[4];
		float renderScale[4];
		float linearLighting[4];
		float wetData[4];
		float wetFade[4];
		float wetBump[4];
		float wetnessData[4];
		float wetnessParams[4];
		float wetnessParams2[4];
		float wetnessDrops[4];
		float wetnessRipple[4];
		float wetnessCamera[4];
		float wetnessFlags[4];
		float puddleData[4];
		float puddleLook[4];
		float puddleShape[4];
		float skyColor[4];
		float skyLowColor[4];
		float horizonColor[4];
		float sunPosition[4];
		float skyData[4];
		float sunAmount[4];
		float sunDiskColor[4];
		float sunsetColor[4];
		float skyIrradiance[9][4];
	} u = {};

	auto copyReq = [&](const char* name, void* dst, size_t bytes) -> bool {
		auto it = TheShaderManager->ConstantsTable.find(name);
		if (it != TheShaderManager->ConstantsTable.end() && it->second) { memcpy(dst, it->second, bytes); return true; }
		return false;
	};
	auto copyOpt = [&](const char* name, void* dst, size_t bytes) {
		auto it = TheShaderManager->ConstantsTable.find(name);
		if (it != TheShaderManager->ConstantsTable.end() && it->second) memcpy(dst, it->second, bytes);
	};

	if (!copyReq("TESR_ProjectionTransform", u.projectionTransform, sizeof(u.projectionTransform)) ||
		!copyReq("TESR_InvProjectionTransform", u.invProjectionTransform, sizeof(u.invProjectionTransform)) ||
		!copyReq("TESR_InvViewTransform", u.invViewTransform, sizeof(u.invViewTransform)) ||
		!copyReq("TESR_ReciprocalResolution", u.reciprocalResolution, sizeof(u.reciprocalResolution)) ||
		!copyReq("TESR_DepthConstants", u.depthConstants, sizeof(u.depthConstants)) ||
		!copyReq("TESR_CameraData", u.cameraData, sizeof(u.cameraData))) return false;
	copyOpt("TESR_ShadowCameraToLightTransformOrtho", u.shadowOrtho, sizeof(u.shadowOrtho));
	auto rsIt = TheShaderManager->ConstantsTable.find("TESR_RenderScale");
	if (rsIt != TheShaderManager->ConstantsTable.end() && rsIt->second) memcpy(u.renderScale, rsIt->second, sizeof(u.renderScale));
	else { u.renderScale[0] = u.renderScale[1] = u.renderScale[2] = u.renderScale[3] = 1.0f; }
	copyOpt("TESR_LinearLighting", u.linearLighting, sizeof(u.linearLighting));
	memcpy(u.wetData, &Constants.Data, sizeof(u.wetData));
	memcpy(u.wetFade, &Constants.Fade, sizeof(u.wetFade));
	memcpy(u.wetBump, &Constants.Bump, sizeof(u.wetBump));
	copyOpt("TESR_WetnessData", u.wetnessData, sizeof(u.wetnessData));
	copyOpt("TESR_WetnessParams", u.wetnessParams, sizeof(u.wetnessParams));
	copyOpt("TESR_WetnessParams2", u.wetnessParams2, sizeof(u.wetnessParams2));
	copyOpt("TESR_WetnessDrops", u.wetnessDrops, sizeof(u.wetnessDrops));
	copyOpt("TESR_WetnessRipple", u.wetnessRipple, sizeof(u.wetnessRipple));
	copyOpt("TESR_WetnessCamera", u.wetnessCamera, sizeof(u.wetnessCamera));
	copyOpt("TESR_WetnessFlags", u.wetnessFlags, sizeof(u.wetnessFlags));
	copyOpt("TESR_PuddleData", u.puddleData, sizeof(u.puddleData));
	copyOpt("TESR_PuddleLook", u.puddleLook, sizeof(u.puddleLook));
	copyOpt("TESR_PuddleShape", u.puddleShape, sizeof(u.puddleShape));
	copyOpt("TESR_SkyColor", u.skyColor, sizeof(u.skyColor));
	copyOpt("TESR_SkyLowColor", u.skyLowColor, sizeof(u.skyLowColor));
	copyOpt("TESR_HorizonColor", u.horizonColor, sizeof(u.horizonColor));
	copyOpt("TESR_SunPosition", u.sunPosition, sizeof(u.sunPosition));
	copyOpt("TESR_SkyData", u.skyData, sizeof(u.skyData));
	copyOpt("TESR_SunAmount", u.sunAmount, sizeof(u.sunAmount));
	copyOpt("TESR_SunDiskColor", u.sunDiskColor, sizeof(u.sunDiskColor));
	copyOpt("TESR_SunsetColor", u.sunsetColor, sizeof(u.sunsetColor));
	copyOpt("TESR_SkyIrradianceObjects", u.skyIrradiance, sizeof(u.skyIrradiance));

	ID3D9NvrExtBuffer* cb = ExtUniforms(extUniforms, &u, sizeof(u));
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	if (!cb || !linear || !point) return false;

	struct Push { UInt32 linearSampler, pointSampler; UInt32 pad[2]; } push = {};
	if (!extPass.Tried) {
		extPass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[9] = {};
		for (UINT i = 0; i < 6; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[6].Set = 2; bindings[6].Binding = 0; bindings[6].Type = NVR_EXT_BIND_UNIFORM_BUFFER; bindings[6].Slot = 0; bindings[6].ViewType = NVR_EXT_VIEW_2D;
		bindings[7].Type = NVR_EXT_BIND_SAMPLER; bindings[7].Slot = 0; bindings[7].PushOffset = 0;
		bindings[8].Type = NVR_EXT_BIND_SAMPLER; bindings[8].Slot = 1; bindings[8].PushOffset = 4;
		extPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("WetnessReflections", bindings, _countof(bindings), sizeof(push), true);
	}
	if (!extPass.Pipeline) return false;

	NVR_EXT_RESOURCE_BINDING res[9] = {};
	res[0].Slot = 0; res[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[0].pResource = rendered;
	res[1].Slot = 1; res[1].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[1].pResource = depth;
	res[2].Slot = 2; res[2].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[2].pResource = normals;
	res[3].Slot = 3; res[3].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[3].pResource = ortho;
	res[4].Slot = 4; res[4].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[4].pResource = shading;
	res[5].Slot = 5; res[5].Type = NVR_EXT_BIND_SAMPLED_IMAGE; res[5].pResource = depthVM;
	res[6].Slot = 0; res[6].Type = NVR_EXT_BIND_UNIFORM_BUFFER; res[6].pResource = cb;
	res[7].Slot = 0; res[7].Type = NVR_EXT_BIND_SAMPLER; res[7].pResource = linear;
	res[8].Slot = 1; res[8].Type = NVR_EXT_BIND_SAMPLER; res[8].pResource = point;
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass.Pipeline, res, _countof(res), &push, sizeof(push), true);
}
