#include "Flashlight.h"
#include "../../core/FlashlightDeferred.h"	// UNOFFICIAL 2026-11-04 the deferred spot light (Usable)
#include "../../core/FlashlightLamp.h"	// UNOFFICIAL 2026-11-04 the Pip-Boy light as its real game lamp


// Settings readers for this file: the section and key exactly as the settings file names them.
static int FlashSettingI(const char* section, const char* key) { return TheSettingManager->GetSettingI(section, key); }
static float FlashSettingF(const char* section, const char* key) { return TheSettingManager->GetSettingF(section, key); }

void FlashlightEffect::UpdateSettings() {
	const char* const mainSection = "Shaders.Flashlight.Main";
	Settings.Dimmer = (std::min)((std::max)(FlashSettingF(mainSection, "Dimmer"), 0.0f), 50.0f);
	Settings.Distance = (std::max)(FlashSettingF(mainSection, "Distance"), 10.0f);
	Settings.attachToWeapon = FlashSettingI(mainSection, "AttachToWeapon") != 0;
	Settings.ConeAngle = 30.0f;	// (2026-11-04: fixed, as the default always was; the full-strength edge is half of it)
	Settings.Offset = NiPoint3(0.0f, 0.0f, 0.0f);

	const char* const deferredSection = "Shaders.Flashlight.Deferred";
	Deferred.Shadows = FlashSettingI(deferredSection, "Shadows") != 0;
	Deferred.ShadowSoftness = (std::min)((std::max)(FlashSettingF(deferredSection, "ShadowSoftness"), 0.0f), 16.0f);
	Deferred.ShadowBias = (std::min)((std::max)(FlashSettingF(deferredSection, "ShadowBias"), 0.0f), 0.1f);
	Deferred.Cookie = (std::min)((std::max)(FlashSettingF(deferredSection, "Cookie"), 0.0f), 1.0f);
	Deferred.Falloff = (std::min)((std::max)(FlashSettingF(deferredSection, "Falloff"), 0.0f), 32.0f);
	Deferred.GameLight = FlashSettingI(deferredSection, "GameLight") != 0;
	Deferred.DebugView = (std::max)(0, (std::min)(3, (int)FlashSettingI(deferredSection, "DebugView")));
	Deferred.TestForceOn = FlashSettingI(deferredSection, "TestForceOn") != 0;

	if (!SpotLight) SpotLight = NiSpotLight::CreateObject();
};

// The weapon types the light does not ride on: melee, grenades, mines and thrown weapons (there is no sight to look down).
static bool IsMeleeWeaponType(UInt8 type) {
	switch (type) {
	case TESObjectWEAP::WeaponType::kWeapType_HandToHandMelee:
	case TESObjectWEAP::WeaponType::kWeapType_OneHandMelee:
	case TESObjectWEAP::WeaponType::kWeapType_TwoHandMelee:
	case TESObjectWEAP::WeaponType::kWeapType_OneHandGrenade:
	case TESObjectWEAP::WeaponType::kWeapType_OneHandMine:
	case TESObjectWEAP::WeaponType::kWeapType_OneHandLunchboxMine:
	case TESObjectWEAP::WeaponType::kWeapType_OneHandThrown:
		return true;
	default:
		return false;
	}
}

// UNOFFICIAL 2026-11-04 (user: "the color of the flashlight should be dictated by the color of your pipboy hud"): the game's "Pipboy" system
// colour (SystemColorManager::GetColor 0x7190A0 on GetSingleton 0x718B60, colour 4: the Pip-Boy's own, set in the game's display settings;
// packed R << 24 | G << 16 | B << 8 | A, MakeRGBA), the game's default amber when there is none.
static NiColor PipboyColor() {
	void* manager = ((void* (__cdecl*)())0x718B60)();
	const UInt32 packed = manager ? ThisStdCall<UInt32>(0x7190A0, manager, 4) : 0;
	if (!packed) return NiColor(1.0f, 182.0f / 255.0f, 66.0f / 255.0f);
	return NiColor(((packed >> 24) & 0xFF) / 255.0f, ((packed >> 16) & 0xFF) / 255.0f, ((packed >> 8) & 0xFF) / 255.0f);
}

void FlashlightEffect::UpdateConstants() {
	if (SpotLight == nullptr) return;
	Settings.Color = PipboyColor();

	// Whether the game has the flashlight on: the test JIP uses (functions_jip/jip_fn_ui.h), with TestForceOn for the auto tests.
	uint8_t* const magicBase = ThisStdCall<uint8_t*>(0x93CCD0, NULL);
	void* const magicSlot = (void*)&(*(MagicItem**)(magicBase + 0x18));
	UInt32 lightIsOn = ThisStdCall<bool>(0x822B90, &Player->magicTarget, magicSlot, 1);
	// UNOFFICIAL 2026-11-04 TestForceOn also switches the game's own Pip-Boy light on, as its key does (PlayerCharacter's input handling: both
	// light effects shown, then the spPipBoyLight ability cast), a few tries in the world: the game light (core/FlashlightLamp.h) is that light
	if (Deferred.TestForceOn && !lightIsOn && Player->parentCell) {
		static int tries = 0;
		static unsigned nextFrame = 0;
		if (tries < 5 && (unsigned)TheFrameRateManager->FrameNumber >= nextFrame) {
			tries++;
			nextFrame = (unsigned)TheFrameRateManager->FrameNumber + 120;
			void* pipboy = ((void* (__cdecl*)())0x705990)();	// Interface::GetPipboy
			void* spell = ThisStdCall<void*>(0x93CCD0, NULL);	// MagicSystem::GetPipBoyLightSpell
			if (pipboy && spell) {
				ThisStdCall<void>(0x7FA310, pipboy, true, true, true);	// FOPipboyManager::ShowPipboyLightEffect (third person)
				ThisStdCall<void>(0x7FA310, pipboy, false, true, true);	// (first person)
				void* caster = (UInt8*)Player + 0x88;	// Actor's MagicCaster
				ThisStdCall<void>(0x815600, caster, spell, false);	// MagicCaster::CastAbility
				Logger::Log("UNOFFICIAL TestForceOn: the Pip-Boy light switched on (try %d)", tries);
			}
		}
	}
	if (Deferred.TestForceOn) lightIsOn = 1;	// UNOFFICIAL 2026-10-28 TestForceOn (auto tests)

	const bool active = Enabled && lightIsOn != 0;
	spotLightActive = active;
	if (!active) {
		RestorePipBoyLight();
		// off: the light goes to zero dimmer and zero reach
		SpotLight->Dimmer = 0.0f;
		SpotLight->Spec = NiColor(0.0f, 0.0f, 0.0f);
		SpotLight->CastShadows = false;
		PublishLightConstants(false);
		return;
	}

	NiPoint3 weaponPos;
	NiMatrix33 weaponRot;
	bool meleeWeapon = false;
	if (Player->process->IsWeaponOut() && Player->ActorSkinInfo) {
		const TESObjectWEAP* weapon = Player->ActorSkinInfo->WeaponForm;
		meleeWeapon = !weapon || IsMeleeWeaponType(weapon->weaponType);
	}

	// Down the sights the light belongs on the gun, whatever AttachToWeapon says: the weapon is lined up with the view there, so
	// riding the head puts the beam behind the thing the player is looking past.
	//
	// IsAiming() cannot be used raw: it is false during the attack and follow-through states, so the light alternated between two
	// bones while firing. So it is latched: aiming takes hold at once, and releasing waits for the signal to stay false for a
	// little while (in real seconds, so the hold does not shorten as the frame rate rises). Melee is excluded, as for AttachToWeapon.
	static const double aimHoldSeconds = 0.35;
	const double frameTime = TheFrameRateManager->Time;
	if (Player->IsAiming()) aimingHoldUntil = frameTime + aimHoldSeconds;
	const bool aiming = frameTime < aimingHoldUntil;
	const bool rideWeapon = (Settings.attachToWeapon || aiming) && !meleeWeapon && Player->process->IsWeaponOut();

	if (Player->isThirdPerson) {
		if (rideWeapon && Player->ActorSkinInfo && Player->ActorSkinInfo->WeaponNode) {
			weaponPos = Player->ActorSkinInfo->WeaponNode->m_worldTransform.pos;
			weaponRot = Player->ActorSkinInfo->WeaponNode->m_worldTransform.rot;
		}
		else if (Player->ActorSkinInfo && Player->ActorSkinInfo->HeadNode) {
			// A turn of 90 degrees about Z, then X, applied in the head bone's own frame.
			static const float turnData[3][3] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } };
			NiMatrix33 turn = NiMatrix33();
			for (int row = 0; row < 3; row++)
				for (int column = 0; column < 3; column++) turn.data[row][column] = turnData[row][column];

			const NiAVObject* head = Player->ActorSkinInfo->HeadNode;
			weaponPos = head->m_worldTransform.pos;
			const NiMatrix33 headRot = head->m_worldTransform.rot;
			turn = turn * headRot;	// the turn in the bone's frame
			weaponRot = headRot * turn;
		}
		else {
			weaponPos = WorldSceneGraph->camera->m_worldTransform.pos;
			weaponRot = WorldSceneGraph->camera->m_worldTransform.rot;
		}
	}
	else {
		if (rideWeapon && Player->firstPersonSkinInfo && Player->firstPersonSkinInfo->WeaponNode) {
			weaponPos = Player->firstPersonSkinInfo->WeaponNode->m_worldTransform.pos;
			weaponRot = Player->firstPersonSkinInfo->WeaponNode->m_worldTransform.rot;
		}
		else {
			weaponPos = WorldSceneGraph->camera->m_worldTransform.pos;
			weaponRot = WorldSceneGraph->camera->m_worldTransform.rot;
		}
	}

	// The offset, turned into the direction of the cone.
	const NiPoint3 offset = weaponRot * Settings.Offset;
	weaponPos.x += offset.x;
	weaponPos.y += offset.y;
	weaponPos.z += offset.z;

	// The Pip-Boy light (the game's, a child of the player's node): the flashlight's game lamp with GameLight (core/FlashlightLamp.h; not
	// culled then: the game hands no culled light to the objects), else culled and darkened while the flashlight is on.
	NiNode* playerNode = Player->GetNode();
	const bool drive = FlashlightDeferred::Usable() && Deferred.GameLight;
	for (UINT32 i = 0; playerNode && i < playerNode->m_children.capacity; i++) {
		NiAVObject* child = playerNode->m_children.data[i];
		if (!child || child->GetRTTI() != (void*)0x11F4A98) continue;
		NiPointLight* pipLight = (NiPointLight*)child;
		if (drive) {
			child->m_flags &= ~child->APP_CULLED;
			if (hiddenPipBoyLight != pipLight || !drivenPipBoyLight) {
				if (hiddenPipBoyLight != pipLight || pipLight->Dimmer != 0.0f || pipLight->Spec.r != 0.0f) { hiddenPipBoyDimmer = pipLight->Dimmer; hiddenPipBoySpec = pipLight->Spec; }
				hiddenPipBoyDiff = pipLight->Diff;
				hiddenPipBoyLocal = pipLight->m_localTransform.pos;
				hiddenPipBoyLight = pipLight;
				drivenPipBoyLight = true;
				static bool told = false;
				if (!told) { told = true; Logger::Log("UNOFFICIAL FlashlightLamp: the Pip-Boy light (dimmer %.2f, reach %.0f) is the flashlight's game light while it is on", hiddenPipBoyDimmer, hiddenPipBoySpec.r); }
			}
			DrivePipBoyLight(pipLight, playerNode, weaponPos, weaponRot);
		}
		else {
			child->m_flags |= child->APP_CULLED;
			if (drivenPipBoyLight && hiddenPipBoyLight == pipLight) {	// (GameLight switched off while lit: its own colour and place back first)
				pipLight->Diff = hiddenPipBoyDiff;
				pipLight->m_localTransform.pos = hiddenPipBoyLocal;
				drivenPipBoyLight = false;
				FlashlightLamp::Release();
			}
			if (hiddenPipBoyLight != pipLight || pipLight->Dimmer != 0.0f || pipLight->Spec.r != 0.0f) {
				if (pipLight->Dimmer != 0.0f || pipLight->Spec.r != 0.0f) { hiddenPipBoyDimmer = pipLight->Dimmer; hiddenPipBoySpec = pipLight->Spec; }
				hiddenPipBoyLight = pipLight;
				pipLight->Dimmer = 0.0f;
				pipLight->Spec = NiColor(0, 0, 0);
			}
		}
	}

	NiSpotLight& light = *SpotLight;
	light.CastShadows = WantsShadowMap();
	light.Diff = Settings.Color;
	light.Dimmer = Settings.Dimmer;
	light.m_worldTransform.pos = weaponPos;
	light.m_worldTransform.rot = weaponRot;
	light.m_worldTransform.scale = 1.0f;
	light.OuterSpotAngle = Settings.ConeAngle;
	light.Spec = NiColor(Settings.Distance, 0, 0);	// radius in the r channel

	PublishLightConstants(true);
};

// The Pip-Boy light's own values back once the flashlight is off (only if it is still the player's: the game may have let it go since).
void FlashlightEffect::RestorePipBoyLight() {
	if (!hiddenPipBoyLight) return;
	NiNode* playerNode = Player->GetNode();
	for (UINT32 i = 0; playerNode && i < playerNode->m_children.capacity; i++) {
		if (playerNode->m_children.data[i] != hiddenPipBoyLight) continue;
		hiddenPipBoyLight->Dimmer = hiddenPipBoyDimmer;
		hiddenPipBoyLight->Spec = hiddenPipBoySpec;
		if (drivenPipBoyLight) {
			hiddenPipBoyLight->Diff = hiddenPipBoyDiff;
			hiddenPipBoyLight->m_localTransform.pos = hiddenPipBoyLocal;
		}
		break;
	}
	hiddenPipBoyLight = nullptr;
	drivenPipBoyLight = false;
	FlashlightLamp::Release();
}

// The shared TESR_SpotLight* constants (the spot list ShadowManager draws the shadow map for, and the shaders that read them), from the
// light as placed this call: UpdateConstants runs more than once a frame (frame start, ShaderManager::GetNearbyLights in the shadow pass,
// after the world), each reading the bones again.
void FlashlightEffect::PublishLightConstants(bool abActive) {
	if (!abActive) {
		const D3DXVECTOR4 zero(0.0f, 0.0f, 0.0f, 0.0f);
		TheShaderManager->SpotLightPosition[0] = zero;
		TheShaderManager->SpotLightDirection[0] = zero;
		TheShaderManager->SpotLightColor[0] = zero;
		return;
	}
	D3DXVECTOR4& position = TheShaderManager->SpotLightPosition[0];
	position = SpotLight->m_worldTransform.pos.toD3DXVEC4();
	position.w = SpotLight->Spec.r;	// radius
	TheShaderManager->SpotLightDirection[0] = D3DXVECTOR4(
		SpotLight->m_worldTransform.rot.data[0][0],
		SpotLight->m_worldTransform.rot.data[1][0],
		SpotLight->m_worldTransform.rot.data[2][0],
		SpotLight->OuterSpotAngle);	// outside angle of the cone
	TheShaderManager->SpotLightColor[0] = D3DXVECTOR4(SpotLight->Diff.r, SpotLight->Diff.g, SpotLight->Diff.b, SpotLight->Dimmer);
}

// UNOFFICIAL 2026-11-04 (core/FlashlightLamp.h): the Pip-Boy light as the flashlight's real lamp. At the flashlight (its place under the
// player's node worked back from the world position, and the world position itself for this frame), with the beam's reach, its colour's hue
// and Dimmer as the light pass has them (the game decodes a lamp's colour x dimmer through the same gamma), and no shadow of its own
// (the game's env-map fade takes the first lamp WITHOUT one). The cone is FlashlightLamp's, per draw.
void FlashlightEffect::DrivePipBoyLight(NiPointLight* PipLight, const NiNode* PlayerNode, const NiPoint3& Pos, const NiMatrix33& Rot) {
	const NiTransform& parent = PlayerNode->m_worldTransform;
	const float dx = Pos.x - parent.pos.x, dy = Pos.y - parent.pos.y, dz = Pos.z - parent.pos.z;
	const float inv = parent.scale > 1.0e-4f ? 1.0f / parent.scale : 1.0f;
	PipLight->m_localTransform.pos.x = (parent.rot.data[0][0] * dx + parent.rot.data[1][0] * dy + parent.rot.data[2][0] * dz) * inv;
	PipLight->m_localTransform.pos.y = (parent.rot.data[0][1] * dx + parent.rot.data[1][1] * dy + parent.rot.data[2][1] * dz) * inv;
	PipLight->m_localTransform.pos.z = (parent.rot.data[0][2] * dx + parent.rot.data[1][2] * dy + parent.rot.data[2][2] * dz) * inv;
	PipLight->m_worldTransform.pos = Pos;

	const NiColor& c = Settings.Color;
	const float peak = (std::max)((std::max)(c.r, c.g), (std::max)(c.b, 1.0e-4f));
	float gammaLight = 1.0f;
	if (const PBRShaders* pbr = TheShaderManager->Shaders.PBR) if (pbr->Constants.Linear.x > 1.0f) gammaLight = pbr->Constants.Linear.x;
	const float dimmer = powf(Settings.Dimmer, 1.0f / gammaLight);
	PipLight->Diff = NiColor(c.r / peak, c.g / peak, c.b / peak);
	// the reach (all three, as the game's lights), shortened by the beam's Falloff: the game's lamp falloff is the gentle 1 - d^2/r^2
	const float lampReach = Settings.Distance / sqrtf(1.0f + 0.5f * Deferred.Falloff);
	PipLight->Spec = NiColor(lampReach, lampReach, lampReach);
	PipLight->CastShadows = 0;
	PipLight->Dimmer = dimmer;

	const float outer = D3DXToRadian((std::min)((std::max)(Settings.ConeAngle, 1.0f), 89.0f));
	FlashlightLamp::Lamp = PipLight;
	FlashlightLamp::Dimmer = dimmer;
	FlashlightLamp::Position = D3DXVECTOR3(Pos.x, Pos.y, Pos.z);
	D3DXVECTOR3 axis(Rot.data[0][0], Rot.data[1][0], Rot.data[2][0]);
	D3DXVec3Normalize(&FlashlightLamp::Axis, &axis);
	FlashlightLamp::CosOuter = cosf(outer);
	FlashlightLamp::CosInner = cosf(outer * 0.5f);
}
