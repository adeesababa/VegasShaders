// UNOFFICIAL 2026-11-07 (ptaaa-fog) RT fog visibility: RtFogVisibility.h; the trace shader Compute\Rt\RtFogVisibility.comp (run by the
// helper), its constants and history rules RtFogGrid.h (shared with the RT test host's fogvis scene).

#include "RtFogVisibility.h"
#include "RtCore.h"
#include "RtFogGrid.h"
#include "RtFogLampGrid.h"	// UNOFFICIAL 2026-11-12 LampHazeShadows: the lamps' pass constants and slots
#include "RtDenoise.h"	// UNOFFICIAL 2026-11-07 (perf-history) HistoryClock

namespace {
	const char* const kSection = "Shaders.VolumetricLight.RT";
	const char* const kUserName = "RT fog visibility";

	struct Settings {
		bool	enabled = true;
		RtFogGrid::Settings grid;
		float	fadeLength = 1000.0f;
		int		debugView = 0;		// 1: traced, not used (the cascades: an A/B)
	} S;

	int			shader = -1;
	int			image[2] = { -1, -1 };	// the ping-pong pair: this frame's output, last frame's (the history)
	unsigned	imageColumns = 0, imageRows = 0;
	int			nextOut = 0;
	RtFogGrid::View lastView;
	UInt32		lastTracedFrame = 0xFFFFFFFF;	// the game frame (TheFrameRateManager->FrameNumber) of the last trace
	unsigned	since = 0;					// frames the history has run
	UInt32		frames = 0, noRoom = 0;
	float		tracedDistance = 0.0f, tracedRange = 0.0f;

	void ReadSettings() {
		S.enabled = TheSettingManager->GetSettingI(kSection, "Enabled") != 0;
		S.grid.rays = std::clamp(TheSettingManager->GetSettingI(kSection, "RaysPerCell"), 1, 8);
		S.grid.temporal = std::clamp(TheSettingManager->GetSettingF(kSection, "Temporal"), 0.0f, 0.97f);
		S.grid.temporalMoving = std::clamp(TheSettingManager->GetSettingF(kSection, "TemporalMoving"), 0.0f, 0.97f);
		S.grid.range = (std::max)(500.0f, TheSettingManager->GetSettingF(kSection, "Range"));
		S.grid.rayLength = (std::max)(100.0f, TheSettingManager->GetSettingF(kSection, "RayLength"));
		S.fadeLength = std::clamp(TheSettingManager->GetSettingF(kSection, "FadeLength"), 1.0f, 0.5f * S.grid.range);
		S.debugView = TheSettingManager->GetSettingI(kSection, "DebugView");
		S.grid.debug = 0;
		S.grid.jitter = true;
	}

	// Outdoors, the volumetric light on, the helper already running for another RT effect
	bool Wants() {
		ReadSettings();
		if (!S.enabled || !RtCore::Running()) return false;
		VolumetricLightEffect* light = TheShaderManager->Effects.VolumetricLight;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (!light || !light->Enabled || !shadows || !TheShaderManager->GameState.isExterior || TheShaderManager->GameState.isUnderwater) return false;
		return RtCore::OthersWant(kUserName);
	}

	void Setup() {
		RtFogGrid::Columns(TheRenderManager->width, TheRenderManager->height, imageColumns, imageRows);
		image[0] = RtCore::AddImage("NVR RT fog visibility 0", imageColumns * 8, imageRows * 8, NVR_EXT_SHARED_R16F);
		image[1] = RtCore::AddImage("NVR RT fog visibility 1", imageColumns * 8, imageRows * 8, NVR_EXT_SHARED_R16F);
		lastView.valid = false;
		since = 0;
		lastTracedFrame = 0xFFFFFFFF;
	}

	void Record(IDirect3DDevice9* Device) {
		VolumetricLightEffect* light = TheShaderManager->Effects.VolumetricLight;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (shader < 0 || image[0] < 0 || image[1] < 0 || !light || !shadows || !light->Textures.RtVisibilitySurface) return;
		unsigned columns = 0, rows = 0;
		RtFogGrid::Columns(TheRenderManager->width, TheRenderManager->height, columns, rows);
		// the volumetric light's grid and its copy target must be this grid (they are made from the same screen size)
		if (columns != imageColumns || rows != imageRows || (UINT)light->Constants.GridLayout.x != columns || (UINT)light->Constants.GridLayout.y != rows) return;
		if (RtCore::PassesLeft() < 1) {	// the frame's passes are full (NVR_RT_MAX_PASSES): the cascades this frame
			if ((noRoom++ % 600) == 0) Logger::Log("UNOFFICIAL Vegas Shaders DXVK: RT fog visibility: no pass left in this frame's RT record (%u frames so far): the cascades this frame.", noRoom);
			return;
		}
		const UInt32 frameNow = TheFrameRateManager ? TheFrameRateManager->FrameNumber : 0;
		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		const D3DXVECTOR3 eye = RtCore::Eye();	// the scene is relative to the eye
		RtFogGrid::View now;
		now.eye[0] = (double)TheRenderManager->CameraPosition.x + eye.x;
		now.eye[1] = (double)TheRenderManager->CameraPosition.y + eye.y;
		now.eye[2] = (double)TheRenderManager->CameraPosition.z + eye.z;
		// the view matrix's columns: right (0), up (1), forward (2) (Depth.hlsl toWorld)
		now.forward[0] = view._13; now.forward[1] = view._23; now.forward[2] = view._33;
		now.right[0] = view._11; now.right[1] = view._21; now.right[2] = view._31;
		now.up[0] = view._12; now.up[1] = view._22; now.up[2] = view._32;
		now.p00 = proj._11; now.p11 = proj._22;
		now.maxDistance = light->GridDistance();
		now.columns = columns; now.rows = rows;
		const D3DXVECTOR4& sun = shadows->Constants.SmoothedSunDir;	// toward the sun (the moon at night), as the cascades
		now.sun[0] = sun.x; now.sun[1] = sun.y; now.sun[2] = sun.z;
		now.valid = true;
		const bool sunUp = shadows->Constants.ShadowFade.x < 1.0f;

		RtFogGrid::Settings s = S.grid;
		// UNOFFICIAL 2026-11-07 (perf-history) Temporal / TemporalMoving are last frame's weight at 60 fps: the same decay a second here
		const RtHistoryTime::Clock clock = RtDenoise::HistoryClock();
		s.temporal = RtHistoryTime::Keep(s.temporal, clock);
		s.temporalMoving = RtHistoryTime::Keep(s.temporalMoving, clock);
		s.range = (std::min)(s.range, (std::max)(512.0f, TheSettingManager->GetSettingF("Main.Main.RayTracing", "SceneRadius")));
		const int atlas = RtCore::AlphaAtlasImage();
		s.alphaTested = RtCore::AlphaTested() && atlas >= 0;
		s.mask = 0xFFu;	// land, objects, alpha-tested, actors, the far LOD: every caster the RT sun shadows see
		if (!(lastTracedFrame != 0xFFFFFFFF && lastTracedFrame + 1 == frameNow && RtFogGrid::Continues(now, lastView))) since = 0;
		const float weight = RtFogGrid::HistoryWeight(since, s.temporal);
		RtFogGrid::PassConstants c;
		RtFogGrid::Fill(c, now, lastView, weight, sunUp, s, frames);

		const int out = image[nextOut], hist = image[nextOut ^ 1];
		const int images[3] = { out, hist, s.alphaTested ? atlas : hist };	// [2]: binding 4, a stand-in (never read) without alpha-tested casters
		if (!RtCore::AddPass(shader, images, 3, 0x1, &c, sizeof(c), (columns * 8 + 7) / 8, (rows * 8 + 7) / 8)) return;
		if (!RtCore::CopyOut(out, light->Textures.RtVisibilitySurface, columns * 8, rows * 8)) return;
		lastTracedFrame = frameNow;
		lastView = now;
		since++;
		nextOut ^= 1;
		tracedDistance = now.maxDistance;
		tracedRange = s.range;
		if (frames++ == 0 || (frames % 3600) == 0)
			Logger::Log("UNOFFICIAL Vegas Shaders DXVK: RT fog visibility traced (frame %u): grid %u x %u x 64 out to %.0f, %d ray%s a cell (%u rays), Temporal %.2f (weight %.2f), "
				"range %.0f (fade %.0f), alpha-tested casters %s, DebugView %d.", frames, columns, rows, now.maxDistance, s.rays, s.rays > 1 ? "s" : "",
				columns * rows * 64u * (unsigned)s.rays, s.temporal, weight, s.range, S.fadeLength, s.alphaTested ? "tested" : "not in the scene", S.debugView);
	}
}

// UNOFFICIAL 2026-11-12 LampHazeShadows ([Shaders.VolumetricFog.Interiors] LampHazeShadows; RtFogVisibility.h RtFogLamps): the second
// rider, indoors. The fog (VolumetricFogEffect::SetLampHaze) hands over the lamps it lights the air with each frame; the frame after,
// at the RT point, this user traces their visibility along every view (Compute\Rt\RtFogLamps.comp, constants and slots RtFogLampGrid.h)
// and copies it out into its own texture, which the fog binds on stage 11 (free indoors: the volumetric light's grid is outdoors only).
namespace {
	const char* const kLampUser = "RT fog lamps";
	const char* const kLampRtSection = "Shaders.ShadowsInteriors.RT";	// the RT lamp shadows' ray rules (read only), so the air and the walls agree

	struct LampState {
		int			shader = -1;
		int			image[2] = { -1, -1 };	// the ping-pong pair
		int			nextOut = 0;
		unsigned	cell = 8, columns = 0, rows = 0;	// as at the helper's start (Setup)
		RtFogLampGrid::Slots slots;			// the lamps of the texture's tiles (the last trace's)
		RtFogLampGrid::Lamp submitted[RtFogLampGrid::kSlots];	// the fog's lamps of its last frame
		int			submitCount = 0;
		UInt32		submitFrame = 0;
		bool		tracedOnce = false;
		UInt32		tracedFrame = 0;
		unsigned	since = 0;				// frames the history has run
		UInt32		frames = 0, noRoom = 0;
		IDirect3DTexture9* texture = nullptr;	// 4 x 4 tiles of columns x rows, R16F
		IDirect3DSurface9* surface = nullptr;
		unsigned	textureW = 0, textureH = 0;
		double		lastEye[3] = { 0.0, 0.0, 0.0 };
		float		lastForward[3] = { 0.0f, 1.0f, 0.0f };
		bool		lastValid = false;
		int			lastLamps = 0, lastSamples = 0;	// (the status line)
		float		lastWeight = 0.0f;
	} L;

	UInt32 LampFrameNow() { return TheFrameRateManager ? TheFrameRateManager->FrameNumber : 0; }

	// The texture holds a trace of the last few frames (a frame whose RT record was full keeps the one before)
	bool LampUsable() { return L.texture && L.tracedOnce && LampFrameNow() - L.tracedFrame <= 8u; }

	// Indoors, the fog asked within the last two frames, the helper already running for another RT effect. (Every early return comes
	// before OthersWant: the sun's user above asks the same question outdoors, and each must answer the other's without asking back.)
	bool LampWants() {
		if (L.shader < 0 || L.submitCount <= 0 || !RtCore::Running()) return false;
		if (LampFrameNow() - L.submitFrame > 2u) return false;	// LampHazeShadows or LampHaze off, outdoors, the fog off: nothing asked
		if (TheShaderManager->GameState.isExterior || TheShaderManager->GameState.isUnderwater) return false;
		return RtCore::OthersWant(kLampUser);
	}

	void LampSetup() {
		VolumetricFogEffect* fog = TheShaderManager->Effects.VolumetricFog;
		L.cell = (unsigned)std::clamp(fog ? fog->lampHazeShadowCell : 8, 4, 32);
		RtFogLampGrid::Tile(TheRenderManager->width, TheRenderManager->height, L.cell, L.columns, L.rows);
		L.image[0] = RtCore::AddImage("NVR RT fog lamps 0", L.columns * RtFogLampGrid::kTilesAcross, L.rows * RtFogLampGrid::kTilesAcross, NVR_EXT_SHARED_R16F);
		L.image[1] = RtCore::AddImage("NVR RT fog lamps 1", L.columns * RtFogLampGrid::kTilesAcross, L.rows * RtFogLampGrid::kTilesAcross, NVR_EXT_SHARED_R16F);
		L.slots = RtFogLampGrid::Slots();
		L.tracedOnce = false;
		L.lastValid = false;
		L.since = 0;
	}

	void LampRecord(IDirect3DDevice9* Device) {
		VolumetricFogEffect* fog = TheShaderManager->Effects.VolumetricFog;
		if (L.shader < 0 || L.image[0] < 0 || L.image[1] < 0 || !fog || !Device || L.submitCount <= 0) return;
		unsigned columns = 0, rows = 0;
		RtFogLampGrid::Tile(TheRenderManager->width, TheRenderManager->height, L.cell, columns, rows);
		if (!columns || !rows || columns != L.columns || rows != L.rows) return;	// not the screen the images were made for
		if (RtCore::PassesLeft() < 1) {	// the frame's passes are full (NVR_RT_MAX_PASSES): the fog keeps the last trace (LampUsable)
			if ((L.noRoom++ % 600) == 0) Logger::Log("UNOFFICIAL Vegas Shaders DXVK: RT fog lamps (LampHazeShadows): no pass left in this frame's RT record (%u frames so far): the last trace is kept.", L.noRoom);
			return;
		}
		const unsigned w = columns * RtFogLampGrid::kTilesAcross, h = rows * RtFogLampGrid::kTilesAcross;
		if (!L.texture || !L.surface || L.textureW != w || L.textureH != h) {
			if (L.surface) { L.surface->Release(); L.surface = nullptr; }
			if (L.texture) { L.texture->Release(); L.texture = nullptr; }
			L.tracedOnce = false;
			if (FAILED(Device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &L.texture, nullptr)) || !L.texture ||
				FAILED(L.texture->GetSurfaceLevel(0, &L.surface)) || !L.surface) {
				if (L.texture) { L.texture->Release(); L.texture = nullptr; }
				L.surface = nullptr;
				static bool failLogged = false;
				if (!failLogged) { failLogged = true; Logger::Log("[ERROR] UNOFFICIAL RT fog lamps (LampHazeShadows): could not create the %u x %u visibility texture: the lamps' light in the fog stays unshadowed.", w, h); }
				return;
			}
			L.textureW = w; L.textureH = h;
		}

		const UInt32 frameNow = LampFrameNow();
		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		// the view matrix's columns: right (0), up (1), forward (2) (Depth.hlsl toWorld; as RtFogVisibility's Record above)
		const float forward[3] = { view._13, view._23, view._33 }, right[3] = { view._11, view._21, view._31 }, up[3] = { view._12, view._22, view._32 };
		const D3DXVECTOR3 eyeScene = RtCore::Eye();	// the scene is relative to the eye
		const double eye[3] = { (double)TheRenderManager->CameraPosition.x + eyeScene.x, (double)TheRenderManager->CameraPosition.y + eyeScene.y,
			(double)TheRenderManager->CameraPosition.z + eyeScene.z };

		// The history is last frame's value of the same texel (a whole view's number: nothing to reproject), so it counts fully only while
		// the camera stands still; moving, at most half (two frames' lag), and not at all after a frame without a trace
		if (!(L.tracedOnce && L.tracedFrame + 1 == frameNow)) L.since = 0;
		bool still = L.lastValid;
		if (still) {
			const double dx = eye[0] - L.lastEye[0], dy = eye[1] - L.lastEye[1], dz = eye[2] - L.lastEye[2];
			still = dx * dx + dy * dy + dz * dz < 1.0 && forward[0] * L.lastForward[0] + forward[1] * L.lastForward[1] + forward[2] * L.lastForward[2] > 0.99999f;
		}
		float temporal = std::clamp(fog->lampHazeShadowTemporal, 0.0f, 0.95f);
		if (!still) temporal = (std::min)(temporal, 0.5f);
		// (perf-history) the setting is last frame's weight at 60 fps: the same decay a second here
		const float weight = RtFogGrid::HistoryWeight(L.since, RtHistoryTime::Keep(temporal, RtDenoise::HistoryClock()));

		RtFogLampGrid::Settings s;
		s.samples = std::clamp(fog->lampHazeShadowSamples, 1, 8);
		s.weight = weight;
		s.jitter = true;
		s.core = fog->lampHazeCore;
		// the rays' rules are the RT lamp shadows' (RtLampShadows.h Record: LampStop, SkipLampModels + LampModelDistance, SkipLampGlass)
		s.lampStop = (std::max)(TheSettingManager->GetSettingF(kLampRtSection, "LampStop"), 0.0f);
		const bool skipModels = TheSettingManager->GetSettingI(kLampRtSection, "SkipLampModels") != 0;
		s.modelDistance = skipModels ? (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Forward", "LampModelDistance"), 0.0f) : 0.0f;
		unsigned farMask = 0xEFu;	// without the alpha-tested casters (0x10): the shader adds them when it tests them
		if (TheSettingManager->GetSettingI(kLampRtSection, "SkipLampGlass")) farMask &= ~0x04u;	// RtLampShadows::MaskGlass
		s.farMask = farMask;
		s.nearMask = skipModels ? (farMask & ~0x08u) : farMask;	// RtLampShadows::MaskLampModel
		const int atlas = RtCore::AlphaAtlasImage();
		s.alphaTested = RtCore::AlphaTested() && atlas >= 0;

		// the fog's lamps into the tiles' slots (a lamp keeps its slot, so its history); committed only when the pass is recorded
		RtFogLampGrid::Slots next = L.slots;
		const unsigned kept = RtFogLampGrid::Assign(next, L.submitted, L.submitCount);
		RtFogLampGrid::PassConstants c;
		RtFogLampGrid::Fill(c, forward, right, up, proj._11, proj._22, eye, columns, rows, next, weight > 0.0f ? kept : 0u, s, L.frames);
		if (c.grid[3] < 1.0f) return;	// no lamp
		const int out = L.image[L.nextOut], hist = L.image[L.nextOut ^ 1];
		const int images[3] = { out, hist, s.alphaTested ? atlas : hist };	// [2]: binding 4, a stand-in (never read) without alpha-tested casters
		if (!RtCore::AddPass(L.shader, images, 3, 0x1, &c, sizeof(c), (w + 7) / 8, (rows * RtFogLampGrid::TileRows(c) + 7) / 8)) return;
		if (!RtCore::CopyOut(out, L.surface, w, h)) return;
		L.slots = next;
		L.tracedFrame = frameNow;
		L.tracedOnce = true;
		L.since++;
		L.nextOut ^= 1;
		for (int i = 0; i < 3; i++) { L.lastEye[i] = eye[i]; L.lastForward[i] = forward[i]; }
		L.lastValid = true;
		L.lastLamps = L.submitCount; L.lastSamples = s.samples; L.lastWeight = weight;
		if (L.frames++ == 0 || (L.frames % 3600) == 0)
			Logger::Log("UNOFFICIAL Vegas Shaders DXVK: RT fog lamps traced (LampHazeShadows, frame %u): %d lamp(s), %u x %u cells a lamp (one per %u pixels), %d point(s) a cell "
				"(at most %u rays), last frame's weight %.2f, lamp stop %.0f, lamp models skipped over the last %.0f, alpha-tested casters %s.", L.frames, L.submitCount, columns, rows, L.cell,
				s.samples, (unsigned)L.submitCount * columns * rows * (unsigned)(s.samples + 1), weight, s.lampStop, s.modelDistance, s.alphaTested ? "tested" : "not in the scene");
	}
}

namespace RtFogLamps {
	void Submit(const D3DXVECTOR4* WorldReach, int Count) {
		L.submitCount = 0;
		if (!WorldReach) return;
		for (int i = 0; i < Count && L.submitCount < (int)RtFogLampGrid::kSlots; i++) {
			RtFogLampGrid::Lamp& lamp = L.submitted[L.submitCount++];
			lamp.world[0] = WorldReach[i].x; lamp.world[1] = WorldReach[i].y; lamp.world[2] = WorldReach[i].z;
			lamp.reach = WorldReach[i].w;
		}
		L.submitFrame = LampFrameNow();
	}

	float Code(const D3DXVECTOR4& WorldReach) {
		if (!LampUsable()) return 0.0f;
		RtFogLampGrid::Lamp lamp;
		lamp.world[0] = WorldReach.x; lamp.world[1] = WorldReach.y; lamp.world[2] = WorldReach.z;
		lamp.reach = WorldReach.w;
		const int slot = RtFogLampGrid::Find(L.slots, lamp);
		return slot < 0 ? -1.0f : (float)(slot + 1 + 32 * (int)L.cell);
	}

	IDirect3DTexture9* Texture() { return LampUsable() ? L.texture : nullptr; }

	std::string StatusText() {
		char t[320];
		if (L.submitCount <= 0 || LampFrameNow() - L.submitFrame > 2u)
			sprintf_s(t, "RT fog lamps off ([Shaders.VolumetricFog.Interiors] LampHazeShadows false, LampHaze off, or outdoors): frames traced %u", L.frames);
		else
			sprintf_s(t, "RT fog lamps on (%s): %d lamp(s), tile %u x %u (one cell per %u pixels), %d point(s) a cell, weight %.2f, frames traced %u%s, frames without a pass left %u",
				!RtCore::Running() ? "the RT core is not running: unshadowed" : RtCore::LeftOut(kLampUser) ? "its images did not fit: unshadowed" : "rides along with the other RT effects",
				L.lastLamps, L.columns, L.rows, L.cell, L.lastSamples, L.lastWeight, L.frames, LampUsable() ? " (in use)" : "", L.noRoom);
		return t;
	}
}

namespace RtFogVisibility {
	void Register() {
		shader = RtCore::AddShader("RtFogVisibility");
		// Order 2: set up after the others (the first left out when the images do not fit). UNOFFICIAL 2026-11-11 RidesEarly: with
		// NVR_RT_EARLY_PT its Record goes with the path tracer's world-start submit (it copies nothing in: camera, sun, settings only), so
		// outdoors the frame needs no second exchange; its copy out lands in CollectEarly, before the volumetric light's grid reads it
		RtCore::AddUser({ kUserName, Wants, Setup, Record, 0, 2, true });
		// UNOFFICIAL 2026-11-12 LampHazeShadows: the lamps' rider, after the sun's (Order 3: the first left out when the images do not
		// fit). Not RidesEarly: it is recorded at the RT point of the frame as before (indoors the early submit gains it nothing)
		L.shader = RtCore::AddShader("RtFogLamps");
		RtCore::AddUser({ kLampUser, LampWants, LampSetup, LampRecord, 0, 3, false });
	}

	D3DXVECTOR4 Constant(float GridDistance) {
		const UInt32 frameNow = TheFrameRateManager ? TheFrameRateManager->FrameNumber : 0;
		if (lastTracedFrame != frameNow || S.debugView == 1 || !S.enabled) return D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
		const float mode = fabsf(GridDistance - tracedDistance) <= 0.01f * (std::max)(tracedDistance, 1.0f) ? 1.0f : 2.0f;
		return D3DXVECTOR4(mode, tracedRange, S.fadeLength, tracedDistance);
	}

	std::string StatusText() {
		if (!S.enabled) return "RT fog visibility off ([Shaders.VolumetricLight.RT] Enabled false): the shafts and the fog's sun from the cascades";
		const UInt32 frameNow = TheFrameRateManager ? TheFrameRateManager->FrameNumber : 0;
		char t[320];
		sprintf_s(t, "RT fog visibility on (%s): grid %u x %u, frames traced %u, last f%u%s, RaysPerCell %d, Temporal %.2f, Range %.0f, DebugView %d, frames without a pass left %u",
			!RtCore::Running() ? "the RT core is not running: the cascades" : "rides along with the other RT effects", imageColumns, imageRows, frames,
			lastTracedFrame == 0xFFFFFFFF ? 0u : lastTracedFrame, lastTracedFrame == frameNow ? " (this frame)" : "", S.grid.rays, S.grid.temporal, tracedRange, S.debugView, noRoom);
		return t;
	}
}
