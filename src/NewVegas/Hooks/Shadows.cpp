// Shadow hooks: NVR's shadow maps drawn via an entry detour on TESMain::DrawWorld_UpdateOffscreenBuffers (0x00871290),
// and the name of the leaves node of a SpeedTree.

#include "../../core/GpuProfiler.h"
#include "../../core/SessionLog.h"	// 2026-11-04: CRASH lines' stage

// The game's own shadow render, which the detoured function (0x00871290) calls after building NVR's shadow maps.
static constexpr UInt32 kGameRenderShadowMaps = 0x871A50;
RenderShadowMapFn RenderShadowMap = (RenderShadowMapFn)0x00871290;

void __fastcall RenderShadowMapHook(void* apThis, UInt32) {
	// UNOFFICIAL TAA: the jitter covers the world scene only, on the understanding that shadow maps are rendered before it.
	// If that ever stops being true, the cascades get fitted to a jittered frustum and shadow edges shimmer -- say so.
	static bool jitterWarned = false;
	if (!jitterWarned) {
		TAAEffect* taa = TheShaderManager->Effects.TAA;
		if (taa && taa->IsJitterActive()) {
			Logger::Log("[WARNING] TAA : shadow maps rendered while the world camera was jittered; cascades will shimmer");
			jitterWarned = true;
		}
	}

	{
		SessionLog::StageScope stage("shadow maps");
		TheShadowManager->RenderShadowMaps();
	}

	// Then the game's shadow setup that DrawWorld_UpdateOffscreenBuffers ran (VS still skips the original body).
	static GpuTimer gameShadowTimer("Game call 0x871A50");
	GpuProfileScope scope(gameShadowTimer, TheRenderManager->device);
	CdeclCall(kGameRenderShadowMaps);
}

// The second child of a tree's node holds its leaves; name it so it can be told apart later.
static void NameTreeLeaves(BSTreeNode* TreeNode) {
	auto* leavesChild = TreeNode->m_children.data[1];
	leavesChild->SetName("Leaves");
}

__declspec(naked) void LeavesNodeNameHook() {
	__asm {
		mov		dword ptr [ebp - 70h], eax
		push	eax
		call	NameTreeLeaves
		pop		eax
		jmp		Jumpers::Shadows::LeavesNodeNameReturn
	}
}
