#pragma once
#include "DxvkLoader.h"

// UNOFFICIAL 2026-10-26 (deep/fork-debug-capture): tools to make a broken frame inspectable. Off by default, nothing runs in
// normal play. Plain-English guide: docs/next/FORK-DEBUG-CAPTURE.md.
//
// 1. GPU debug labels ([Main.Develop.Main] GpuDebugLabels): every F10 GPU timer scope (GpuProfileScope: NVR's effects, shadow
//    maps, the game's world scene / first person / image space, the extension's passes, ...), every effect's Render and each sun
//    cascade opens a named group through D3DPERF_BeginEvent / D3DPERF_EndEvent. On native D3D9 these reach only a D3D9 tool
//    (PIX) and are near free; under DXVK 3.0.1 they become Vulkan debug labels (vkCmdBeginDebugUtilsLabelEXT) ONLY when the game
//    is started with the environment variable DXVK_DEBUG=markers (d3d9_annotation.cpp: the annotation object is made only with
//    DxvkDebugFlag::Markers); otherwise DXVK returns at once. The extension's own passes are recorded into the same CS stream
//    (EmitCs) as the labels, so they land inside the group of the NVR scope that issued them: no DXVK patch is needed.
//    D3DPERF_* are looked up in the d3d9.dll the game loaded (GetModuleHandle, never loaded by us).
//
// 2. RenderDoc capture trigger: RequestCapture asks RenderDoc's in-app API (RENDERDOC_GetAPI) for a capture of the next frame(s),
//    ONLY if renderdoc.dll is already in the process (the game was started from RenderDoc, or RenderDoc injected into it). NVR
//    never loads or ships RenderDoc. Without it: one log line, nothing else. Callers: the [Main.Develop.Main] RenderDocCaptureKey
//    hotkey (Render.cpp) and the shot-list command 'capture' (SettingShots.h). Tick (once a frame) logs the file RenderDoc wrote.
//    RenderDoc captures Vulkan (DXVK) but not native D3D9: on native D3D9 the request is accepted and nothing is written; Tick
//    says so after a while.

#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstdio>

namespace GpuDebug {

	inline bool Labels = false;			// [Main.Develop.Main] GpuDebugLabels (SettingManager::LoadSettings)
	inline int CaptureKey = 0;			// [Main.Develop.Main] RenderDocCaptureKey: DirectInput key code, 0 = no hotkey
	inline int CaptureFrames = 1;		// [Main.Develop.Main] RenderDocCaptureFrames: frames per hotkey capture (1-10)

	// ---- 1. labels ----

	typedef int (WINAPI* BeginEventFn)(D3DCOLOR, LPCWSTR);
	typedef int (WINAPI* EndEventFn)();
	typedef DWORD (WINAPI* GetStatusFn)();
	struct PerfApi { BeginEventFn Begin = nullptr; EndEventFn End = nullptr; GetStatusFn Status = nullptr; bool Tried = false; };
	inline PerfApi& Perf() {
		static PerfApi api;
		if (!api.Tried) {
			api.Tried = true;
			if (HMODULE d3d9 = DxvkLoader::Runtime()) {
				api.Begin = (BeginEventFn)GetProcAddress(d3d9, "D3DPERF_BeginEvent");
				api.End = (EndEventFn)GetProcAddress(d3d9, "D3DPERF_EndEvent");
				api.Status = (GetStatusFn)GetProcAddress(d3d9, "D3DPERF_GetStatus");
			}
			if (!api.Begin || !api.End) api.Begin = nullptr, api.End = nullptr;
			Logger::Log("GPU DEBUG LABELS: %s (D3DPERF_GetStatus = %u: 1 = a capture tool or DXVK_DEBUG=markers is listening, 0 = the labels go nowhere)",
				api.Begin ? "on, D3DPERF_BeginEvent/EndEvent found in d3d9.dll" : "NOT available: the loaded d3d9.dll has no D3DPERF_BeginEvent",
				api.Status ? (unsigned)api.Status() : 0u);
		}
		return api;
	}

	// One colour per name (a small hash), so the groups are told apart at a glance in RenderDoc's event browser.
	inline D3DCOLOR ColorOf(const char* name) {
		UINT32 h = 2166136261u;
		for (const char* c = name; c && *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
		return D3DCOLOR_XRGB(96 + (h & 127), 96 + ((h >> 8) & 127), 96 + ((h >> 16) & 127));
	}

	// Returns true when a group was opened (End must follow).
	inline bool Begin(const char* name, const char* suffix = nullptr) {
		PerfApi& api = Perf();
		if (!api.Begin || !name) return false;
		while (*name == ' ') name++;	// the F10 timers indent sub-steps with spaces
		wchar_t wide[128];
		size_t n = 0;
		for (const char* c = name; *c && n < 80; c++) wide[n++] = (wchar_t)(unsigned char)*c;
		if (suffix) for (const char* c = suffix; *c && n < 126; c++) wide[n++] = (wchar_t)(unsigned char)*c;
		wide[n] = 0;
		api.Begin(ColorOf(name), wide);
		return true;
	}
	inline void End() { PerfApi& api = Perf(); if (api.End) api.End(); }

	// A named group for the lifetime of the object; nothing at all when GpuDebugLabels is off.
	class LabelScope {
	public:
		explicit LabelScope(const char* name, const char* suffix = nullptr) : Open(Labels && Begin(name, suffix)) {}
		~LabelScope() { if (Open) End(); }
		LabelScope(const LabelScope&) = delete;
		LabelScope& operator=(const LabelScope&) = delete;
	private:
		bool Open;
	};

	// ---- 2. RenderDoc ----

	// The part of renderdoc_app.h (RenderDoc's in-app API, MIT licence) used here: the 1.1.2 function table, in its order.
	// Only the entries up to TriggerMultiFrameCapture are declared; the rest are never touched.
	typedef void (__cdecl* RdGenericFn)();
	struct RenderDocApi112 {
		void (__cdecl* GetAPIVersion)(int* major, int* minor, int* patch);
		RdGenericFn SetCaptureOptionU32, SetCaptureOptionF32, GetCaptureOptionU32, GetCaptureOptionF32;
		RdGenericFn SetFocusToggleKeys, SetCaptureKeys, GetOverlayBits, MaskOverlayBits;
		RdGenericFn RemoveHooks, UnloadCrashHandler, SetCaptureFilePathTemplate, GetCaptureFilePathTemplate;
		uint32_t (__cdecl* GetNumCaptures)();
		uint32_t (__cdecl* GetCapture)(uint32_t index, char* filename, uint32_t* pathLength, uint64_t* timestamp);
		void (__cdecl* TriggerCapture)();
		RdGenericFn IsTargetControlConnected, LaunchReplayUI, SetActiveWindow, StartFrameCapture, IsFrameCapturing, EndFrameCapture;
		void (__cdecl* TriggerMultiFrameCapture)(uint32_t numFrames);
	};
	typedef int (__cdecl* RenderDocGetApiFn)(int version, void** outApi);
	enum { kRenderDocApi112 = 10102 };

	struct CaptureState {
		RenderDocApi112* Api = nullptr;
		bool Tried = false, WarnedMissing = false;
		uint32_t CountBefore = 0;
		int WaitFrames = 0;			// > 0: a capture was asked for; Tick counts down while it waits for the file
		char Why[64] = "";
	};
	inline CaptureState& Capture() { static CaptureState s; return s; }

	inline RenderDocApi112* RenderDoc() {
		CaptureState& s = Capture();
		if (s.Api) return s.Api;
		HMODULE rd = GetModuleHandleA("renderdoc.dll");	// already loaded by RenderDoc itself; never LoadLibrary
		if (!rd) {
			if (!s.WarnedMissing) {
				s.WarnedMissing = true;
				Logger::Log("RENDERDOC: capture asked for, but renderdoc.dll is not loaded in the game (start the game from RenderDoc; see docs/next/FORK-DEBUG-CAPTURE.md). Nothing done.");
			}
			return nullptr;
		}
		if (s.Tried) return nullptr;
		s.Tried = true;
		RenderDocGetApiFn getApi = (RenderDocGetApiFn)GetProcAddress(rd, "RENDERDOC_GetAPI");
		void* api = nullptr;
		if (!getApi || !getApi(kRenderDocApi112, &api) || !api) {
			Logger::Log("RENDERDOC: renderdoc.dll is loaded but RENDERDOC_GetAPI(1.1.2) failed; no captures from Vegas Shaders.");
			return nullptr;
		}
		s.Api = (RenderDocApi112*)api;
		int major = 0, minor = 0, patch = 0;
		s.Api->GetAPIVersion(&major, &minor, &patch);
		Logger::Log("RENDERDOC: in-app API %d.%d.%d found (renderdoc.dll was already loaded)", major, minor, patch);
		return s.Api;
	}

	// Asks RenderDoc to capture the next 'frames' frames (1 = the next frame). why: for the log ("hotkey", a shot's slug).
	inline bool RequestCapture(int frames, const char* why) {
		RenderDocApi112* api = RenderDoc();
		if (!api) return false;
		CaptureState& s = Capture();
		if (frames < 1) frames = 1;
		if (frames > 10) frames = 10;
		s.CountBefore = api->GetNumCaptures();
		if (frames == 1) api->TriggerCapture();
		else api->TriggerMultiFrameCapture((uint32_t)frames);
		s.WaitFrames = 600;
		sprintf_s(s.Why, "%s", why ? why : "");
		Logger::Log("RENDERDOC: capture of the next %d frame%s asked for (%s)", frames, frames == 1 ? "" : "s", s.Why);
		return true;
	}

	// Once a frame (Render.cpp): logs the capture file once RenderDoc has written it.
	inline void Tick() {
		CaptureState& s = Capture();
		if (s.WaitFrames <= 0 || !s.Api) return;
		const uint32_t count = s.Api->GetNumCaptures();
		if (count > s.CountBefore) {
			for (uint32_t i = s.CountBefore; i < count; i++) {
				char path[MAX_PATH] = "";
				uint32_t length = MAX_PATH;
				uint64_t stamp = 0;
				if (s.Api->GetCapture(i, path, &length, &stamp)) Logger::Log("RENDERDOC: capture written (%s): %s", s.Why, path);
			}
			s.CountBefore = count;
			s.WaitFrames = 0;
			return;
		}
		if (--s.WaitFrames == 0)
			Logger::Log("RENDERDOC: no capture file after 600 frames (%s). RenderDoc captures Vulkan only: is the game running on the DXVK d3d9.dll?", s.Why);
	}
}
