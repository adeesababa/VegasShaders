#pragma once
// UNOFFICIAL 2026-11-07 (perf-mirrorfix) the hair's tint for the ray-traced scene. The game draws hair grey and tints it in the hair pixel
// shaders (SM3002 / SM3003: HairTint, c2: albedo x 2 x lerp(0.5, HairTint, vertex colour .y)); the traced scene took the grey texture
// alone ("white hair in mirrors", docs/next/RT-REAL-TEXTURES.md). Render.cpp notes c2 after each world draw of a skinned geometry with a
// hair shader bound (NoteBind in SetShadersHook, NoteDraw after RenderPassSkinned), keyed by the geometry; RtCore::AddSkinned reads it
// (Find) and packs a TINTED record (RtScenePick.h ActorRecordFlags). Only while RtCore asks (Wanted: actors and hit textures on).
#include <d3d9.h>
#include <unordered_map>
#include <cstring>
#include <cstdint>

namespace RtHairTint {
	struct Tint { float rgb[3]; unsigned frame; };
	inline bool Wanted = false;			// RtCore: actors + hit textures on (set each scene walk)
	inline bool BoundHair = false;		// the pixel shader bound last is a hair shader
	inline std::unordered_map<const void*, Tint> Tints;
	inline unsigned Frame = 0;			// RtCore's frame counter (NoteDraw stamps it)

	// SetShadersHook: the pixel shader bound now (its record's name, e.g. "SM3003.pso")
	inline void NoteBind(const char* pixelShaderName) {
		BoundHair = pixelShaderName && (!strcmp(pixelShaderName, "SM3003.pso") || !strcmp(pixelShaderName, "SM3002.pso"));
	}

	// After a skinned pass of the world scene drew: the tint it drew with
	inline void NoteDraw(const void* geometry, IDirect3DDevice9* device) {
		if (!Wanted || !BoundHair || !geometry || !device) return;
		float c[4];
		if (FAILED(device->GetPixelShaderConstantF(2, c, 1))) return;
		Tint& t = Tints[geometry];
		t.rgb[0] = c[0]; t.rgb[1] = c[1]; t.rgb[2] = c[2]; t.frame = Frame;
		if (Tints.size() > 4096) {	// geometries that went away (a cell change): drop the ones not drawn for a while
			for (auto it = Tints.begin(); it != Tints.end(); ) { if (Frame - it->second.frame > 600u) it = Tints.erase(it); else ++it; }
		}
	}

	// UNOFFICIAL 2026-11-07 (perf-mirrorfix2, test 10: the RT hair stayed white) at DRAW time (Render.cpp MaterialDraw, the skinned partition
	// draws): the hair is drawn by the sorted blended list, which never comes through RenderPassSkinnedHook, so NoteDraw above never saw it.
	// pixelShader: the bound pass's pixel shader name; geometry: the game's current render pass's geometry.
	inline unsigned DrawNotes = 0;
	inline void NoteHairDraw(const void* geometry, const char* pixelShader, IDirect3DDevice9* device) {
		if (!Wanted || !geometry || !device) return;
		const bool hair = pixelShader && (!strcmp(pixelShader, "SM3003.pso") || !strcmp(pixelShader, "SM3002.pso"));
		if (!hair) return;
		const bool saved = BoundHair;
		BoundHair = true;
		NoteDraw(geometry, device);
		BoundHair = saved;
		DrawNotes++;
	}

	// The tint noted for this geometry within the last `maxAge` frames
	inline bool Find(const void* geometry, float rgb[3], unsigned maxAge = 600u) {
		auto it = Tints.find(geometry);
		if (it == Tints.end() || Frame - it->second.frame > maxAge) return false;
		std::memcpy(rgb, it->second.rgb, sizeof(float) * 3);
		return true;
	}

	inline void Clear() { Tints.clear(); }

	// ---- UNOFFICIAL 2026-11-08 (perf-reflfix3, test 11: the player's hair white in the chrome sphere, first person) -----------------------
	// In first person the game never draws the player's third-person body: its hair never comes through NoteDraw / NoteHairDraw, so its tint
	// was never noted (or expired 600 frames after the last third-person view) and the traced hair went in untinted (grey). Two changes:
	//   - a noted tint is kept as long as its geometry's entry lives (Lookup: no age limit; the map is still pruned when it grows);
	//   - the player's hair with no noted tint takes the player's hair colour from the game's data (TESNPC::hairColor, what the hair shader
	//     is given as HairTint: nv-rs actor.rs, Sunny Smiles' HCLR (66, 28, 15) / 255 = her HairTint), for a part whose shader property has
	//     the Hair flag (hair, beards, brows: not eyes or fringed clothes). Its byte order: kNVSE GetHairColor reads 0x00RRGGBB; the ESM's
	//     HCLR stores R, G, B: the first time a noted tint and the colour meet (the player drawn in third person) the order that matches is
	//     kept (Calibrate); until then 0x00RRGGBB.
	inline int ColorOrder = 0;			// 0: 0x00RRGGBB, 1: R in the low byte
	inline bool OrderCalibrated = false;
	inline void Unpack(uint32_t packed, int order, float rgb[3]) {
		const float r = float((packed >> 16) & 0xFF), g = float((packed >> 8) & 0xFF), b = float(packed & 0xFF);
		rgb[0] = (order ? b : r) / 255.0f; rgb[1] = g / 255.0f; rgb[2] = (order ? r : b) / 255.0f;
	}
	// a noted tint (the hair shader's c2) and the same actor's packed hair colour: keep the byte order whose colour is nearer
	inline void Calibrate(uint32_t packed, const float noted[3]) {
		if (OrderCalibrated) return;
		float a[3], b[3];
		Unpack(packed, 0, a); Unpack(packed, 1, b);
		float ea = 0.0f, eb = 0.0f;
		for (int k = 0; k < 3; k++) { ea += (a[k] - noted[k]) * (a[k] - noted[k]); eb += (b[k] - noted[k]) * (b[k] - noted[k]); }
		if (ea == eb) return;   // (a grey colour says nothing about the order)
		ColorOrder = eb < ea ? 1 : 0;
		OrderCalibrated = true;
	}
	inline bool Lookup(const void* geometry, float rgb[3]) {
		auto it = Tints.find(geometry);
		if (it == Tints.end()) return false;
		std::memcpy(rgb, it->second.rgb, sizeof(float) * 3);
		return true;
	}
	// The tint of an actor's alpha-tested part: noted from its draw, else (the player's own hair: playerHair) the player's hair colour
	// (packed). hairFlag: the part's shader property has the Hair flag. 0 none, 1 noted, 2 the player's colour.
	inline int For(const void* geometry, bool hairFlag, bool playerHair, uint32_t packed, float rgb[3]) {
		if (Lookup(geometry, rgb)) {
			if (playerHair && hairFlag) Calibrate(packed, rgb);
			return 1;
		}
		if (playerHair && hairFlag) { Unpack(packed, ColorOrder, rgb); return 2; }
		return 0;
	}
}
