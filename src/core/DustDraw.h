#pragma once

// 2026-11-04 dust on top surfaces ([Shaders.PBR.Dust], Includes/ObjectDust.hlsl): which object draws may take dust. User: "the dust
// seems to appear on our character". The shaders cannot tell a skinned mesh: SKIN is a vertex-shader define, and one pixel shader
// draws skinned and static meshes alike. So each object pass is told here, pixel bool constant b14 (NVR_DustDraw; 2026-11-05: was the float
// c169.x, now ForwardCloudShadows' NVR_CloudSun, and no float register below c224 is free): true for a static mesh in the world scene (the RenderPassStandard hook), 0 for skinned meshes -- people, creatures,
// armour, the first-person arms (the RenderPassSkinned hook) -- and for first person, the reflections and anything else. Sent only
// when it changes, and again with the frame's first object (effects between frames may use the high registers). Off at startup
// (not compiled into the shaders): nothing is sent.
namespace DustDraw {
	inline bool Checked = false, Compiled = false;
	inline int Sent = -1;

	inline void BeginFrame() {
		if (!Checked) {
			Checked = true;
			Compiled = TheSettingManager->GetSettingI("Shaders.PBR.Dust", "Enabled") != 0;	// as ShaderRecord compiled it (DUST_ON)
		}
		Sent = -1;
	}

	// One object pass about to be drawn. allow: a static mesh drawn in the world scene.
	inline void ForDraw(IDirect3DDevice9* device, bool allow) {
		if (!Compiled) return;
		const BOOL flag = allow ? TRUE : FALSE;
		if (flag == Sent) return;
		Sent = flag;
		device->SetPixelShaderConstantB(14, &flag, 1);
	}
}
