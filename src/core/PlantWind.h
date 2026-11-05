#pragma once

// UNOFFICIAL 2026-10-20 plant wind ([Shaders.Plants.Wind], OFF by default: a look change the user judges). The Mojave's plants move
// in the same wind as the grass field wind (core/GrassWind.h, [Shaders.Grass.Wind]): the same direction (sky->windDirection), the same
// strength source (sky->windSpeed, through the game's own grass wind curve), the same clock and the same gust pattern (fronts
// (0.5 + 0.5 sin(clock / 4 - P.K))^2 x a patch wave 45 degrees off the wind, P the WORLD position), so a gust rolls through the grass
// and the bushes together. Plan, limits and results: bench\deep-roadmap\evidence\plantwind-LOG.md.
//
// Two kinds of plant:
// - SpeedTree bushes (TREE records: WastelandShrub01, the commonest plant, ~18,000 placed): their leaves are camera-facing cards drawn
//   by NVR's STLEAF001/003.vso, which already carry the game's leaf swing (WindA/WindB). There the passing gust (at the bush's base,
//   world position) strengthens the game's swing and adds a flutter, as the sandstorm's TESR_LeafStorm does, and optionally leans the
//   leaves downwind (LeafLean, 0 by default: the branches and fronds are the GAME's shaders -- NVR has no replacement -- and do not
//   follow). Global per frame: TESR_PlantLeafWind* (the shaders run nothing of it while it is off). The sun's shadow maps draw the
//   leaves with Shadows\ShadowMap.vso: the same boost and lean there (not the small flutter: the game's leaf parameters leave no place
//   to add it without changing the shadow's off path).
// - Static plants (STAT records: Joshua trees, yuccas, banana yucca, creosote, sagebrush, ... by texture keyword, as cloth): bent
//   by the field wind, rooted at the ground (weight 0 at the plant's base rising with the square of the height above it; tall plants
//   stiffer, so a Joshua tree's crown sways less than a bush's top), plus a small flutter. Done by the cloth wind's machinery
//   (core/ClothWind.h): per draw c248-c253 (the cloth's registers; c248.x = -1 marks a plant draw, which the cloth code never runs on),
//   compiled into NVR's object vertex shaders (ObjectTemplate.hlsl) and the shadow map shader only when Enabled at startup
//   (PLANT_WIND_ON), its SAFETY rule (every pass of an object moves it the same way, or none does: an object with a vanilla vertex
//   shader in any pass stays still) and its clean-up after each draw (AfterDraw). Every number sent is worked out from the object's
//   world transform and the frame's wind, so every pass of a frame, the shadow maps included, gets the same move.
// Cacti, agave and stumps stay rigid (Exclude); sea plants are animated by their meshes; far LOD trees stay still (as in vanilla).
// Ideas: the grass field wind and the cloth wind here; Sousa, 'Vegetation Procedural Animation and Shading in Crysis', GPU Gems 3
// ch. 16 (2007): bend weight rising with the square of the height, travelling waves for the gusts.
#include <string>
#include <vector>
#include <unordered_map>
#include "ClothWind.h"
#include "GrassWind.h"

namespace PlantWind {

	struct SettingsStruct {
		bool	Enabled = false;		// live for the leaves; the static plants need it on at startup (PLANT_WIND_ON)
		float	Bend = 6.0f;			// units a passing gust leans a static plant's top, per 100 units of its height, at a middling wind (2026-10-20: 4 was
										// hard to see in clear weather, 1.6 units at the noon spot's wind; evidence\plantwind-LOG.md)
		float	StiffHeight = 200.0f;	// plants taller than this bend less (Joshua trees): lean x (H / 100) / (1 + (H / StiffHeight)^2)
		float	Flutter = 0.25f;		// a static plant's own quiver, as a fraction of its gust lean (Bend)
		float	LeafGust = 1.5f;		// SpeedTree leaves: a passing gust's crest multiplies the game's leaf swing by (1 + LeafGust)
		float	LeafFlutter = 0.12f;	// SpeedTree leaves: radians of extra flutter (x the wind strength)
		float	LeafLean = 0.0f;		// SpeedTree leaves: units the crest leans the leaves downwind (the branches do not follow: 0 = none)
		float	LeafHeight = 120.0f;	// SpeedTree leaves: model units above the bush's base where the lean is full (rising with the square)
		int		Log = 0;				// diagnostic: 1 = plant draws, matches and the wind every 600 frames
	};
	inline SettingsStruct S;
	inline bool CompiledIn = false, CompiledInRead = false;	// Enabled at startup: PLANT_WIND_ON in the object vertex shaders
	inline bool On = false;			// this frame: Enabled and outdoors
	inline bool StaticsOn = false;	// ...and the static plants (CompiledIn)
	inline std::vector<std::string> Words, Excluded;
	inline std::string WordsFrom, ExcludeFrom;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float Strength = 1.0f;		// the weather's wind as the game's grass gets it, 1 at a middling wind (see BeginFrame)
	inline float GrassMin = 5.0f, GrassMax = 125.0f;
	inline double ClockNow = 0.0;		// the grass wind's clock this frame (radians, 0..8 pi, TimeOffset applied)
	inline float K[2] = {}, Dir[2] = { 0.0f, 1.0f };	// gust wave vector (radians per unit), wind direction (world)
	// SpeedTree leaves (TESR_PlantLeafWind, ...2, ...3: Includes/PlantWind.hlsl): global per frame
	inline D3DXVECTOR4 LeafA = { 0, 0, 0, 0 }, LeafB = { 0, 0, 0, 0 }, LeafC = { 0, 0, 0, 0 };
	// diagnostic counts (Log 1, per 600 frames)
	inline UInt32 Frames = 0, DrawsLit = 0, DrawsShadow = 0, SkipSafety = 0, SkipShadowVanilla = 0, SkipNoRoot = 0;
	inline UInt32 MatchesLogged = 0;

	// Flutter: a third travelling wave, short (about 140 units) and partly vertical, so the parts of one plant quiver out of step.
	static const float kFlutterWave[3] = { 0.031f, -0.023f, 0.019f };
	static const double kTwoPi = 6.283185307179586;

	inline double Wrap(double a) { a = fmod(a, kTwoPi); return a < 0.0 ? a + kTwoPi : a; }

	inline void RegisterConstants() {	// GrassShaders::RegisterConstants (always called at startup)
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind", &LeafA);
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind2", &LeafB);
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind3", &LeafC);
	}

	inline void SplitList(const char* text, std::vector<std::string>& out) { ClothWind::SplitList(text, out); }
	inline bool IsPlantTexture(const char* path) {
		if (!path || !path[0]) return false;
		const std::string p = ClothWind::Norm(path);
		bool hit = false;
		for (const std::string& w : Words) if (!w.empty() && p.find(w) != std::string::npos) { hit = true; break; }
		if (!hit) return false;
		for (const std::string& x : Excluded) if (!x.empty() && p.find(x) != std::string::npos) return false;
		return true;
	}

	// Once a frame (Render.cpp, before the scene walk, after ClothWind::BeginFrame): the settings (live), the shared wind and clock.
	inline void BeginFrame(const Sky* sky, bool exterior) {
		const char* section = "Shaders.Plants.Wind";
		if (!CompiledInRead) { CompiledInRead = true; CompiledIn = TheSettingManager->GetSettingI(section, "Enabled") != 0; ClothWind::PlantsCompiledIn = CompiledIn; }
		S.Enabled = TheSettingManager->GetSettingI(section, "Enabled") != 0;
		On = S.Enabled && exterior;
		StaticsOn = On && CompiledIn;
		ClothWind::PlantsOn = StaticsOn;
		GrassWind::ClockWanted = S.Enabled;
		// (the whole text: GetSettingS cuts a string at 79 characters, as ClothWind::BeginFrame reads its lists)
		// UNOFFICIAL 2026-11-04: read with the wind off too: MaterialBuffer's plant class (Render.cpp MaterialForPass) uses the same lists.
		auto readList = [&](const char* key, std::string& from, std::vector<std::string>& out) {
			SettingManager::Configuration::ConfigNode node;
			if (!TheSettingManager->Config.FillNode(&node, section, key)) return;
			const std::string text = node.LongValue;
			if (from != text) { from = text; SplitList(text.c_str(), out); ByTexture.clear(); }
		};
		readList("Keywords", WordsFrom, Words);
		readList("Exclude", ExcludeFrom, Excluded);
		if (!S.Enabled) { LeafA = LeafB = LeafC = D3DXVECTOR4(0, 0, 0, 0); return; }
		S.Bend = std::clamp(TheSettingManager->GetSettingF(section, "Bend"), 0.0f, 100.0f);
		S.StiffHeight = std::clamp(TheSettingManager->GetSettingF(section, "StiffHeight"), 20.0f, 5000.0f);
		S.Flutter = std::clamp(TheSettingManager->GetSettingF(section, "Flutter"), 0.0f, 4.0f);
		S.LeafGust = std::clamp(TheSettingManager->GetSettingF(section, "LeafGust"), 0.0f, 10.0f);
		S.LeafFlutter = std::clamp(TheSettingManager->GetSettingF(section, "LeafFlutter"), 0.0f, 1.0f);
		S.LeafLean = std::clamp(TheSettingManager->GetSettingF(section, "LeafLean"), 0.0f, 50.0f);
		S.LeafHeight = std::clamp(TheSettingManager->GetSettingF(section, "LeafHeight"), 1.0f, 5000.0f);
		S.Log = TheSettingManager->GetSettingI(section, "Log");

		// The grass field wind's own settings and clock (GrassWind::Update runs once a frame, whichever of the grass and the plants calls
		// it first, with the same inputs -- the sandstorm's share as Grass.cpp gives it): the plants share its gusts even with the grass
		// wind itself off (ClockWanted).
		GrassWind::ReadSettings();
		const SandstormEffect* Sandstorm = TheShaderManager->Effects.Sandstorm;
		const float storm = Sandstorm && Sandstorm->Enabled && Sandstorm->GrassStorm[0] > 0.0f ? Sandstorm->Intensity : 0.0f;
		GrassWind::Update(sky, exterior, storm);
		if (!On) { LeafA = LeafB = LeafC = D3DXVECTOR4(0, 0, 0, 0); return; }
		ClockNow = (double)GrassWind::ClockNow;
		Dir[0] = GrassWind::WindDir[0]; Dir[1] = GrassWind::WindDir[1];
		const double k = 2.0 * 3.14159265358979323846 / GrassWind::S.GustSize;
		K[0] = (float)(Dir[0] * k); K[1] = (float)(Dir[1] * k);
		// The strength source: the grass's own sway size, fGrassWindMagnitudeMin + windSpeed x (Max - Min) (ShadowSceneNode::PreOnVisible,
		// the grass log), relative to a middling wind (speed 0.5) -- so plants and grass grow alike with the weather's wind.
		const float mn = *Pointers::Settings::GrassWindMagnitudeMin, mx = *Pointers::Settings::GrassWindMagnitudeMax;
		if (mn >= 0.0f && mx >= mn && mx > 0.0f && mx < 5000.0f) { GrassMin = mn; GrassMax = mx; }
		const float mid = GrassMin + 0.5f * (GrassMax - GrassMin);
		Strength = mid > 1e-3f ? (GrassMin + GrassWind::WindSpeed * (GrassMax - GrassMin)) / mid : 1.0f;
		// The leaves: gust wave vector, clock, on; flutter, lean, 1 / lean height, gust boost; wind direction.
		LeafA = D3DXVECTOR4(K[0], K[1], (float)ClockNow, 1.0f);
		LeafB = D3DXVECTOR4(S.LeafFlutter * Strength, S.LeafLean * Strength, 1.0f / S.LeafHeight, S.LeafGust);
		LeafC = D3DXVECTOR4(Dir[0], Dir[1], 0.0f, 0.0f);
		if (S.Log && ++Frames >= 600) {
			const float* c4 = (const float*)0x01200550;	// TallGrassShader's grouped constants: c4 WindData (the grass log)
			Logger::Log("UNOFFICIAL Plant wind: %s, statics %s; wind direction (%.3f %.3f) speed %.3f -> strength %.3f (game grass wind min %.1f max %.1f, "
				"its c4 now %.3f %.3f size %.2f); clock %.3f (the grass wind's, rate %.3f rad/s); per frame: static plant draws %.1f lit, %.1f shadow, skipped "
				"%.1f (a pass without Vegas Shaders' vertex shader) + %.1f (shadow, same) + %.1f (no root node); leaves: gust x%.2f, flutter %.3f rad, lean %.2f",
				On ? "on" : "off", StaticsOn ? "on" : (CompiledIn ? "off (indoors)" : "NOT compiled in (needs a restart)"), Dir[0], Dir[1], GrassWind::WindSpeed, Strength,
				GrassMin, GrassMax, c4[0], c4[1], c4[2], ClockNow, GrassWind::Rate, DrawsLit / (float)Frames, DrawsShadow / (float)Frames, SkipSafety / (float)Frames,
				SkipShadowVanilla / (float)Frames, SkipNoRoot / (float)Frames, LeafB.w, LeafB.x, LeafB.y);
			Frames = DrawsLit = DrawsShadow = SkipSafety = SkipShadowVanilla = SkipNoRoot = 0;
		}
	}

	// The plant's root: the model's own top node (BSFadeNode), whose position is the reference's -- the plant's base on the ground.
	inline const NiAVObject* RootOf(const NiAVObject* geometry) {
		const NiAVObject* o = geometry;
		for (int i = 0; i < 8 && o; i++) {
			if (*(const UInt32*)o == 0x010A8F90) return o;	// BSFadeNode
			o = o->m_parent;
		}
		return nullptr;
	}

	// UNOFFICIAL 2026-10-30 the ray tracing scene's share (RtCore AddGeometry, [Main.Main.RayTracing] PlantSway): a static plant's gust
	// lean for its traced instance, so traced shadows / AO / reflections sway with the drawn plant. The BLAS stays as loaded; the lean
	// goes into the instance transform as a shear rooted at the plant's base: P' = P + lean (P.z - base) / H, lean the drawn top's
	// gust move (world units, along the wind; the gust taken at the plant's root, as the shader's waves barely change across a plant).
	// Linear in height where the shader bends with (h / H)^2: base and top agree, half height is lean / 4 off; the flutter and the
	// top's drop (both a fraction of a unit) are left out. False when the drawn plant does not move (wind off, indoors, not a plant,
	// first-person, skinned) -- the same tests as ForGeometry, without its per-pass SAFETY bookkeeping.
	inline bool RtLean(const NiGeometry* geometry, float lean[2], float& base, float& H) {
		if (!StaticsOn || !geometry || geometry->skinInstance) return false;
		BSShaderProperty* property = (BSShaderProperty*)const_cast<NiGeometry*>(geometry)->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) return false;
		const NiSourceTexture* diffuse = ((BSShaderPPLightingProperty*)property)->ppTextures[0] ? ((BSShaderPPLightingProperty*)property)->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) return false;
		bool plant;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) plant = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();
			plant = IsPlantTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), plant };
		}
		if (!plant) return false;
		const NiAVObject* root = RootOf(geometry);
		if (!root || !root->m_kWorldBound) return false;
		base = root->m_worldTransform.pos.z;
		H = (std::max)(root->m_kWorldBound->Center.z + root->m_kWorldBound->Radius - base, 10.0f);
		const float top = S.Bend * Strength * (H * 0.01f) / (1.0f + (H / S.StiffHeight) * (H / S.StiffHeight));
		const double X = root->m_worldTransform.pos.x, Y = root->m_worldTransform.pos.y;
		const double along = X * K[0] + Y * K[1];
		const double front = 0.5 + 0.5 * sin(ClockNow * 0.25 - along);
		const double patch = 0.75 + 0.25 * sin(ClockNow * 0.25 - (X * 0.5 * (K[0] - K[1]) + Y * 0.5 * (K[1] + K[0])));
		const float g = (float)(top * front * front * patch);
		lean[0] = Dir[0] * g; lean[1] = Dir[1] * g;
		return true;
	}

	// Right after ClothWind::ForGeometry for every object draw (the game's passes, RenderPassStandardHook, and NVR's sun shadow
	// passes, RenderPass::RenderAccum): a static plant's move in c248-c253, or nothing (ForGeometry has cleaned c248, or put a cloth's
	// values there: this draw is cloth). ClothWind::AfterDraw cleans it after the draw.
	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false, bool firstPerson = false) {
		if (!StaticsOn || !device || !geometry || firstPerson || ClothWind::NonZeroSent || geometry->skinInstance) return;
		// SAFETY (ClothWind.h): every pass of the object with an NVR vertex shader that carries the code, or it stays still. The same rule
		// and bookkeeping as ClothWind::ForGeometry (with the cloth on both run it: the same answers).
		bool boundAnswer = false;
		if (!shadowPass) {
			ClothWind::NoteTentative(geometry, ClothWind::CurrentVSCapable);
			boundAnswer = !ClothWind::SeenLast(geometry) && ClothWind::CurrentVSCapable && !ClothWind::VanillaLast(geometry);
		}
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) return;	// BSShaderPPLightingProperty, Lighting30ShaderProperty
		const NiSourceTexture* diffuse = ((BSShaderPPLightingProperty*)property)->ppTextures[0] ? ((BSShaderPPLightingProperty*)property)->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) return;
		bool plant;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) plant = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();
			plant = IsPlantTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), plant };
			if (plant && (MatchesLogged < 32 || S.Log)) { MatchesLogged++; Logger::Log("UNOFFICIAL Plant wind: PLANT %s ('%s')", diffuse->ddsPath1, geometry->m_pcName ? geometry->m_pcName : ""); }
		}
		if (!plant) return;
		if (!shadowPass && !boundAnswer && (!ClothWind::SeenLast(geometry) || ClothWind::VanillaLast(geometry))) { SkipSafety++; return; }
		if (shadowPass && ClothWind::VanillaLast(geometry)) { SkipShadowVanilla++; return; }
		const NiAVObject* root = RootOf(geometry);
		if (!root || !root->m_kWorldBound) { SkipNoRoot++; return; }

		// The object's world transform: world = T + s R p (R = rot.data[row][column]); the shader works in the object's own space p.
		const NiTransform& w = geometry->m_worldTransform;
		const float s = w.scale > 1e-4f ? w.scale : 1.0f;
		const float (*R)[3] = w.rot.data;
		const double T[3] = { w.pos.x, w.pos.y, w.pos.z };
		const float base = root->m_worldTransform.pos.z;
		// The plant's height: the top of its root's bound (a sphere, so a little above the real top) over its base.
		const float H = (std::max)(root->m_kWorldBound->Center.z + root->m_kWorldBound->Radius - base, 10.0f);
		const float lean = S.Bend * Strength * (H * 0.01f) / (1.0f + (H / S.StiffHeight) * (H / S.StiffHeight));
		// A world vector v as the shader's dot(p, .): s R^T v; a world direction as object units per world unit: R^T v / s.
		auto toObj = [&](const float* v, float* o, float f) { for (int j = 0; j < 3; j++) o[j] = f * (R[0][j] * v[0] + R[1][j] * v[1] + R[2][j] * v[2]); };
		const float k1[3] = { K[0], K[1], 0.0f };
		const float k2[3] = { 0.5f * (K[0] - K[1]), 0.5f * (K[1] + K[0]), 0.0f };	// the patch wave, 45 degrees off the wind at 1.4x its length (GrassWind.hlsl)
		const float up[3] = { 0.0f, 0.0f, 1.0f }, dir[3] = { Dir[0], Dir[1], 0.0f };
		float c[24];
		c[0] = -1.0f; c[1] = lean; c[2] = lean * S.Flutter; c[3] = 1.0f / H;
		toObj(k1, &c[4], s);  c[7] = (float)Wrap(ClockNow * 0.25 - (T[0] * K[0] + T[1] * K[1]));
		toObj(k2, &c[8], s);  c[11] = (float)Wrap(ClockNow * 0.25 - (T[0] * k2[0] + T[1] * k2[1]));
		// per plant, a random flutter phase from where it stands (all its parts the same)
		const double rp = sin(root->m_worldTransform.pos.x * 12.9898 + root->m_worldTransform.pos.y * 78.233) * 43758.5453;
		toObj(kFlutterWave, &c[12], s);
		c[15] = (float)Wrap(2.0 * ClockNow + (rp - floor(rp)) * kTwoPi + T[0] * kFlutterWave[0] + T[1] * kFlutterWave[1] + T[2] * kFlutterWave[2]);
		toObj(up, &c[16], s);  c[19] = (float)(T[2] - base);
		toObj(dir, &c[20], 1.0f / s);  c[23] = 0.0f;
		device->SetVertexShaderConstantF(248, c, 6);
		ClothWind::NonZeroSent = true; ClothWind::GrassInC248 = false;	// ClothWind::AfterDraw / Zero clean c248 after this draw
		if (shadowPass) DrawsShadow++; else DrawsLit++;
	}
}
