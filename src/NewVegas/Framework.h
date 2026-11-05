#pragma once
#pragma warning (disable: 4244) //disable warning for possible loss of data in implicit cast between int, float and double

#define DETOURS_INTERNAL
#define assert(a) static_assert(a, "Assertion failed")
#define DIRECTINPUT_VERSION 0x0800
#define D3DXFX_LARGEADDRESS_HANDLE

#include <windows.h>
#include <CommCtrl.h>
#include <string>
#include <regex>
#include <vector>
#include <stack>
#include <map>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <d3d9.h>
#include <d3dx9math.h>
#include <dinput.h>
#include <dsound.h>
#include "../lib/Detours/detours.h"
#include "../lib/Nvidia/nvapi.h"
#include "../lib/Bink/bink.h"
#include "../Base/Logger.h"
#include "../core/SessionLog.h"	// UNOFFICIAL 2026-11-01: session log + frame journal (FRAME_JOURNAL anywhere)
#include "../Base/Types.h"
#include "../Base/SafeWrite.h"
#include "../Base/PluginVersion.h"
#include "../Base/Utils.h"
#include "Plugin.h"
#include "nvse/GameNi.h"
#include "nvse/GameHavok.h"
#include "nvse/Game.h"
#include "Defines.h"
#include "Base.h"
#include "Managers.h"
#include "../core/TerrainErosion.h"
#include "../Core/Hooks/GameCommon.h"
#include "../Core/Hooks/FormsCommon.h"
#include "Hooks/Settings.h"
#include "Hooks/Game.h"
#include "Hooks/ShaderIO.h"
#include "Hooks/Shaders.h"
#include "Hooks/Render.h"
#include "Hooks/Shadows.h"
#include "Hooks/FlyCam.h"
#include "../Core/ReShadeIntegration.h"
