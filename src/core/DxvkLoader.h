#pragma once

// UNOFFICIAL 2026-11-04 DxvkLoader: the DXVK fork's d3d9.dll from a mod manager's mod. Mod Organizer 2 maps only Data, so a
// d3d9.dll in a mod never reaches the game folder, where Windows looks for it. Instead Data\NVSE\Plugins\VSDxvk\ holds it (as
// the DLSS / RT helpers' NvrDlss / NvrRt folders do), with an optional dxvk.conf. At plugin load, before the game makes its
// Direct3D:
// - DXVK_CONFIG_FILE points DXVK at that dxvk.conf (unless the environment already names one);
// - the d3d9.dll there is loaded by full path, as a second module beside Windows' d3d9.dll (which the game already imports);
// - Windows' Direct3DCreate9 (the game's import, and what other plugins' hooks on it chain to) and Direct3DCreate9Ex jump to
//   DXVK's. The originals are never called again.
// A DXVK d3d9.dll in the game folder (ProductName "DXVK", already loaded as the game's import) is blocked the same way: its
// entry points jump to the mod's, so it never makes a device. Any other wrapper there (ENB, ReShade's d3d9.dll, ...) is left as it
// is and the mod's file is not loaded. Nothing is done in the editor or when the file is missing. GetModuleHandle("d3d9.dll")
// still returns the game folder's or Windows' module afterwards: code that asks which d3d9.dll the game runs on uses Runtime().

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace DxvkLoader {
	inline HMODULE Loaded = nullptr;	// the mod's d3d9.dll, when it was loaded and the entry points redirected

	// The d3d9.dll the game's Direct3D comes from.
	inline HMODULE Runtime() { return Loaded ? Loaded : GetModuleHandleA("d3d9.dll"); }

	inline bool JumpTo(void* from, void* to) {
		DWORD old;
		if (!VirtualProtect(from, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
		uint8_t* p = (uint8_t*)from;
		p[0] = 0xE9;
		*(int32_t*)(p + 1) = (int32_t)((uintptr_t)to - (uintptr_t)from - 5);
		VirtualProtect(from, 5, old, &old);
		FlushInstructionCache(GetCurrentProcess(), from, 5);
		return true;
	}

	// True when the file's ProductName starts with "DXVK" (what RenderManager's DXVK check reads too).
	inline bool IsDxvk(const char* path) {
		DWORD ignored = 0;
		const DWORD size = GetFileVersionInfoSizeA(path, &ignored);
		if (!size) return false;
		std::vector<char> info(size);
		if (!GetFileVersionInfoA(path, 0, size, info.data())) return false;
		struct LangCodepage { WORD language, codePage; }* table = nullptr;
		UINT bytes = 0;
		if (!VerQueryValueA(info.data(), "\\VarFileInfo\\Translation", (LPVOID*)&table, &bytes)) return false;
		for (UINT i = 0; i < bytes / sizeof(LangCodepage); i++) {
			char key[64];
			sprintf_s(key, "\\StringFileInfo\\%04x%04x\\ProductName", table[i].language, table[i].codePage);
			char* product = nullptr;
			UINT length = 0;
			if (VerQueryValueA(info.data(), key, (LPVOID*)&product, &length) && product && !strncmp(product, "DXVK", 4)) return true;
		}
		return false;
	}

	inline void Load() {
		const std::string dir = Paths::PluginsDir() + "VSDxvk\\";
		const std::string dll = dir + "d3d9.dll", config = dir + "dxvk.conf";
		if (GetFileAttributesA(dll.c_str()) == INVALID_FILE_ATTRIBUTES) return;	// no mod: nothing to say

		HMODULE system = GetModuleHandleA("d3d9.dll");
		char systemPath[MAX_PATH] = {}, windows[MAX_PATH] = {};
		if (system) GetModuleFileNameA(system, systemPath, MAX_PATH);
		GetSystemWindowsDirectoryA(windows, MAX_PATH);
		const bool windowsOwn = system && !_strnicmp(systemPath, windows, strlen(windows));
		if (!system || (!windowsOwn && !IsDxvk(systemPath))) {
			Logger::Log("DxvkLoader: %s is there, but the game's d3d9.dll is %s, another wrapper: that one is used (remove it from the game folder to use the mod's)",
				dll.c_str(), system ? systemPath : "not loaded");
			return;
		}
		if (!windowsOwn) Logger::Log("DxvkLoader: the game folder's DXVK %s is blocked: the mod's is used", systemPath);

		const bool hasConfig = GetFileAttributesA(config.c_str()) != INVALID_FILE_ATTRIBUTES;
		if (hasConfig && !GetEnvironmentVariableA("DXVK_CONFIG_FILE", nullptr, 0)) SetEnvironmentVariableA("DXVK_CONFIG_FILE", config.c_str());
		HMODULE dxvk = LoadLibraryA(dll.c_str());
		if (!dxvk) { Logger::Log("[ERROR] DxvkLoader: could not load %s (Win32 error %lu): Windows' Direct3D 9", dll.c_str(), GetLastError()); return; }

		int redirected = 0;
		for (const char* name : { "Direct3DCreate9", "Direct3DCreate9Ex" }) {
			void* from = (void*)GetProcAddress(system, name);
			void* to = (void*)GetProcAddress(dxvk, name);
			if (from && to && JumpTo(from, to)) ++redirected;
			else Logger::Log("[ERROR] DxvkLoader: %s not redirected", name);
		}
		if (redirected) Loaded = dxvk;
		char loadedConfig[MAX_PATH] = {};
		GetEnvironmentVariableA("DXVK_CONFIG_FILE", loadedConfig, MAX_PATH);
		Logger::Log("DxvkLoader: %s loaded (%d of 2 entry points redirected; dxvk.conf %s)", dll.c_str(), redirected, loadedConfig[0] ? loadedConfig : "none");
	}
}
