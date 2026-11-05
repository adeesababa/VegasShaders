// UNOFFICIAL 2026-10-18 NewGrass ([Shaders.Grass.NewGrass], OFF by default; core/NewGrass.h; bench\deep-roadmap\evidence\newgrass-DESIGN.md).
// Real blade geometry in place of the game's alpha-tested grass cards, built here from the game's OWN grass instance table: the
// TallGrassShader uploads c20..c247 (up to 228 tufts: xyz position, frac = packed slope normal, w = size step + brightness) for the
// card draw this one replaces, so positions, density, grass types per region and the per-tuft size and brightness all stay the
// game's. Each vertex is 4 bytes (UBYTE4): x tuft index, y blade index, z level along the blade, w side (0 left, 1 right).
// Ideas: Ghost of Tsushima's procedural grass (Eric Wohllaib, GDC 2021: Bezier blades, hashed per-blade variation, side-tilted
// normals, blades per tuft falling with distance); Jahrmann & Wimmer 2017 'Responsive Real-Time Grass Rendering' (width grows with
// distance so fewer blades keep the coverage). D3D9 SM3: no compute or stream-out, so the blade is pure vertex-shader arithmetic.
//
// REGISTER MAP (hand-pinned): the game's own grass constants are read where they are (c0 DiffuseDir, c1 DiffuseColor, c2 ScaleMask,
// c4 WindData, c5 AlphaParam, c6 Ambient, c7 AddlParams, c9-c12 ModelViewProj, c14 FogColor, c15 FogParam, c20-c247 InstanceData);
// ours sit at c248-c251 (NewGrass.h writes them per draw). Literals (def) go to the free registers below c20 (c3, c8, c13, c16-c19).
// No TESR_ constant here on purpose: Shadow.hlsl's pins (c100-c133) would land inside the instance table. The pixel shader
// rebuilds the camera-relative world position from the clip position it gets (GetShadowWorldPos).

float3 DiffuseDir   : register(c0);
float3 DiffuseColor : register(c1);
float3 ScaleMask    : register(c2);
float4 WindData     : register(c4);   // xy direction, z magnitude, w phase (the game's)
float4 AlphaParam   : register(c5);   // z fade start, w fade range
float4 AmbientColor : register(c6);
float4 AddlParams   : register(c7);   // x grass dimmer
row_major float4x4 ModelViewProj : register(c9);
float4 FogColor     : register(c14);
float4 FogParam     : register(c15);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

float4 NG_Lod   : register(c248); // x LodNear (units), y 1 / (LodFar - LodNear), z 1 / WidenDistance, w Droop (a far-leaning blade's tip drops)
float4 NG_Shape : register(c249); // x the plant's height (the card's opaque top, units, before the tuft's size step), y the card's half width, z width / height, w Bend (the curve's outward share)
float4 NG_Misc  : register(c250); // x 1 = blades grow along the slope normal (LFS variant), y blades per tuft, z height scale, w tufts in this draw
float4 NG_Var   : register(c251); // x 1 / (levels - 1), y 1 = only the tufts before the fade start (GrassFirst part 1 of a mixed draw), z flutter, w LengthMin
float4 NG_Prof  : register(c254); // the card's silhouette per height band, bottom to top (share of the half width): the alpha EDGE in the hybrid, the mean alpha radius for replacing blades
float4 NG_Prof2 : register(c255); // x Spread: how far from the clump's centre a strand's root may stand (share of the half width); y 1 = hybrid (the card stays: the strands thin out to NONE at LodFar); z TipReach: tips between half of and this many times the silhouette radius; w Kink: a sideways wobble along the strand (share of its width)
// UNOFFICIAL 2026-10-20 grass field wind ([Shaders.Grass.Wind], Includes/GrassWind.hlsl; NewGrass.h writes it per draw, 0 = off).
// c253 is the one register this shader leaves free (its literals fill c3-c19 and c252); the clock and the card's weight line ride in
// AddlParams.yzw (c7: the game reads only x, the grass dimmer; NewGrass.h sets y-w for its own draw and puts the game's c7 back).
float4 NG_WindLanes : register(c253); // the field wind's lanes, as the card shaders get them: xy the gust wave vector, z gust lean, w flutter

#include "Includes/GrassWind.hlsl"

struct VS_INPUT {
    float4 packed : TEXCOORD0;   // UBYTE4: x tuft, y blade, z level, w side
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 clip     : TEXCOORD0;   // the clip position again (the PS rebuilds the camera-relative world position from it)
    float4 params   : TEXCOORD1;   // x height along the blade 0..1, y side 0..1, z tint seed 0..1, w the tuft's light scale
    float4 normal   : TEXCOORD2;   // xyz world normal of the blade (side-tilted), w grass dimmer
    float4 sunDir   : TEXCOORD3;   // xyz the sun direction the game lights the grass with
    float4 ambient  : TEXCOORD4;   // the game's grass ambient colour
    float4 sun      : TEXCOORD5;   // the game's grass sun colour
    float4 up       : TEXCOORD6;   // xyz the tuft's up (the ground normal the cards are lit with)
    float4 fog      : COLOR0;      // rgb fog colour, w fog amount (as the game's grass VS)
};

// A stable number 0..1 from a position and a salt (the tuft position, so blades keep their place when the engine rebuilds a group).
float Hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    int idx = (int)(IN.packed.x + 0.5f);
    float4 inst = InstanceData[idx];
    float blade = IN.packed.y;
    float t = IN.packed.z * NG_Var.x;                 // 0 root .. 1 tip
    float side = IN.packed.w * 2.0f - 1.0f;           // -1 / +1

    float4 rootClip = mul(ModelViewProj, float4(inst.xyz, 1.0f));
    float depth = rootClip.w;
    // The game's fade metric (REVERSED_DEPTH: length(x, y, w)), the same test GrassFirst's parts use.
    float instDist = length(float3(rootClip.xy, rootClip.w));

    // Blades per tuft fall with distance (LodNear -> LodFar: all -> one); the blade past the budget shrinks away over one blade of
    // budget (no popping as the camera moves), then is folded to a point outside the view (no raster work).
    // (Round 4's first build still had round 1's far-fade line here, keyed on NG_Lod.w, which now carries Droop: every blade folded.)
    float budget = lerp(NG_Misc.y, NG_Prof2.y > 0.5f ? 0.0f : 1.0f, saturate((depth - NG_Lod.x) * NG_Lod.y));
    float keep = saturate(budget - blade);
    bool gone = IN.packed.x >= NG_Misc.w || keep <= 0.0f || (NG_Var.y > 0.5f && instDist > AlphaParam.z);
    [branch] if (gone) {
        VS_OUTPUT none = (VS_OUTPUT)0;
        none.position = float4(2.0f, 2.0f, 0.5f, 1.0f);
        return none;
    }

    // Packed per tuft: orientation (the ground normal) and the light scale, as the game's VS reads them.
    float4 f = frac(inst);
    float lightScale = f.w * 0.75f + 0.25f;
    float3 orient = normalize((f.xyz - 0.5f) * 2.0f);
    float3 up = NG_Misc.x > 0.5f ? normalize(float3(0.0f, 0.0f, 1.0f) + orient) : float3(0.0f, 0.0f, 1.0f);

    // Per-blade variation from the tuft's place and the blade index.
    // (Few distinct literals on purpose: the game's compiler packs them into the free registers and refuses a spill past c255.)
    float2 key = inst.xy * 0.03125f + blade * float2(7.13f, 2.3f);
    float h1 = Hash(key);
    float h2 = Hash(key + 17.17f);
    float h3 = Hash(key.yx + 17.17f);
    float h4 = Hash(key.yx * 1.5f + 7.13f);

    // The tuft's size step, on the axes the grass scales (uniform or height only), as the game.
    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    // The plant's height (the card's opaque top) and half width; a blade's length is a share of it (LengthMin..1): the short ones at
    // the clump's edge make the dome of a bush, the long ones its centre. Nothing stands taller than the card's plant.
    float plantH = NG_Shape.x * scale.z * NG_Misc.z;
    float halfW = NG_Shape.y * scale.x;
    float len = lerp(NG_Var.w, 1.0f, h1);
    float height = plantH * len * keep;
    // The tip lands inside the card's own silhouette at that height: between half of and TipReach times the silhouette radius of the
    // blade's band (the alpha edge in the hybrid, TipReach 1 = never past the card's outline), so the strands fill the card's shape and
    // reach no farther (the bare ground between clumps stays bare).
    float hb = len * 4.0f - 0.5f;
    float rsil = hb < 1.0f ? lerp(NG_Prof.x, NG_Prof.y, saturate(hb)) : hb < 2.0f ? lerp(NG_Prof.y, NG_Prof.z, hb - 1.0f) : lerp(NG_Prof.z, NG_Prof.w, saturate(hb - 2.0f));
    float rTip = halfW * rsil * NG_Prof2.z * (0.5f + 0.5f * h3);
    // The blade radiates from near the clump's centre (Spread of the half width) in its own direction, its face turned outward.
    float ang = h2 * 6.2831853f;
    float3 away = float3(cos(ang), sin(ang), 0.0f);
    float3 outDir = normalize(away - up * dot(away, up));
    float3 base = inst.xyz + outDir * (halfW * NG_Prof2.x * h4);
    float3 sideVec = cross(up, outDir);

    // Wind: the game's per-tuft phase and magnitude (the far cards sway with it), a little jitter per blade, a flutter across.
    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float swing = sin(phase + h1 * 1.5f);
    float3 flutterAcross = sideVec * (sin(phase * 2.3f + h2 * 6.2831853f) * WindData.z * NG_Var.z);
    float3 wind = float3(WindData.xy * (swing * WindData.z), 0.0f) + flutterAcross;
    // UNOFFICIAL 2026-10-20 grass field wind (NG_WindLanes; GrassWind.hlsl): the strand then moves exactly as the card it stands on moves
    // at the same height (below, after its shape): the game's sway leaves the curve here, only NewGrass's own flutter across stays.
    const bool fieldWind = dot(NG_WindLanes.xy, NG_WindLanes.xy) > 0.0f;
    [flatten] if (fieldWind) wind = flutterAcross;

    // The blade: a quadratic Bezier from its root up and outward to its tip on the silhouette, curving (Bend = the control point's
    // outward share), a far-leaning blade dropping its tip (Droop), the wind pulling the top.
    float lean = rTip / max(height, 1.0f);
    float droop = NG_Lod.w * saturate(lean - 0.5f) * height;
    float3 p0 = base;
    float3 p2 = base + up * (height - droop) + outDir * rTip + wind;
    float3 p1 = base + up * (height * 0.6f) + outDir * (rTip * NG_Shape.w) + wind * 0.3f;
    float u = 1.0f - t;
    float3 pos = (u * u) * p0 + (2.0f * u * t) * p1 + (t * t) * p2;
    float3 tangent = normalize(2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1));

    // Width: a share of the plant's height (a short blade is as wide at its root as a long one), tapering to the tip, growing with
    // distance (fewer, wider blades keep the far coverage). Kink: a sideways wobble from level to level, so a strand zigzags like a
    // dry twig instead of running smooth.
    float width = plantH * NG_Shape.z * (0.6f + 0.6f * h4) * (1.0f + depth * NG_Lod.z) * (1.0f - t * t) * keep;
    pos += sideVec * (side * 0.5f * width + NG_Prof2.w * width * sin(t * 9.0f + h2 * 6.2831853f) * (0.5f + 0.5f * h3));

    // UNOFFICIAL 2026-10-20 grass field wind: the card's move at this point's height (GRASS23x00*.vso: the game's sway without the
    // per-blade jitter, turned onto the wind, plus the gusts and flutter of THIS tuft, times the card's weight at that height: the line
    // AddlParams.z + w x height fitted to its vertex alpha^2 against its own model height, NewGrass.h), so the strands stay with
    // their clump. AddlParams.y = the clock.
    [branch] if (fieldWind) {
        float rise = dot(pos - base, up);
        float weight = saturate(AddlParams.z + AddlParams.w * rise / scale.z);
        pos += GrassWindMove(inst, NG_WindLanes, AddlParams.y, sin(phase), WindData, weight, rise);
    }

    float3 normal = normalize(cross(sideVec, tangent));
    normal = normalize(normal + sideVec * (side * 0.3f));   // a rounded blade: the normal tilts toward each side

    OUT.position = mul(ModelViewProj, float4(pos, 1.0f));
    OUT.clip = OUT.position;
    OUT.params = float4(t * len, IN.packed.w, h4, lightScale);   // x: the height on the PLANT, so each blade shows the card's colour there
    OUT.normal = float4(normal, AddlParams.x);
    OUT.sunDir = float4(DiffuseDir, 0.0f);
    OUT.ambient = float4(AmbientColor.rgb, 1.0f);
    OUT.sun = float4(DiffuseColor, 1.0f);
    // The cards' sun term uses the packed ground normal (orient) whatever the variant: the PS blends it with the blade's own normal.
    OUT.up = float4(orient, 0.0f);

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    return OUT;
};
