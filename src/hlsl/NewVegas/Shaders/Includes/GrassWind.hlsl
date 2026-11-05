// UNOFFICIAL 2026-10-20 grass field wind ([Shaders.Grass.Wind], core/GrassWind.h; OFF by default; bench\deep-roadmap\evidence\
// grasswind-LOG.md). The grass moves like a field in the wind: gusts roll across it along the weather's wind like waves through
// wheat, each tuft flutters a little on its own so neighbours never move in lockstep, and the game's own sway is turned onto the
// weather's wind direction -- the direction cloth uses (core/ClothWind.h: sky->windDirection), so the grass and the coats agree.
// (The game sways all grass along one fixed axis whatever the weather: Sky::UpdateWind 0x0063C490 sets NiPoint2::UNIT_Y.)
// Strength: multiples of the game's own sway at that vertex (WindData.z x vertex alpha^2). The game makes WindData.z
// fGrassWindMagnitudeMin + sky->windSpeed x (Max - Min) (ShadowSceneNode::PreOnVisible; INI 5 / 125): the weather's wind speed,
// the same source cloth scales by, and each grass model's own authored sway (its vertex alpha) is kept.
//
// Per draw NVR sets (core/GrassWind.h):
//   lanes  x, y the gust wave vector: the wind direction x 2 pi / GustSize (radians per unit); z the gust lean, w the flutter (both
//          times the game's sway). x = y = 0: off -- the callers run none of this (a branch on lanes.xy): the game's sway exactly.
//          The card shaders (GRASS23x00*.vso) have no free constant register: the lanes ride in the 4th column of the inverse view
//          matrix rows (c252.w-c255.w), which the shadow lookup never reads (GetShadowWorldPos returns mul(v, InvView).xyz). Unwritten
//          (the effect off) that column is the matrix's own (0, 0, 0, 1). NewGrass.vso has them in c253.
//   clock  radians, integrated on the CPU in double precision and wrapped at 8 pi (no jitter after long play, as the cloth's phases):
//          the gusts move at clock / 4 and the flutter at 2 x clock, so both wrap whole. The card shaders read it from c19.z, which
//          the sandstorm's grass wind shares (GrassWind.h blends the clock's rate to the storm's while a storm blows).
//
// Shape: the gust front is (0.5 + 0.5 sin(clock / 4 - P.K))^2 (sharp crests, the troughs at rest, so the field leans downwind as each
// gust passes and straightens behind it), times a patch wave 45 degrees off the wind at 1.4x the length (0.75 + 0.25 sin), so the
// fronts break into patches instead of running in endless straight bands. A sum of travelling waves as Sousa's vegetation animation
// (GPU Gems 3 ch. 16, 2007) and the cloth wind here; sin rather than ClothTri: the grass shaders hold no literal 3.
// P is the tuft's WORLD position (InstanceData xy: TallGrassShaderProperty::AddGrass, nv-rs grass.rs), so the waves run on across the
// grass groups without a seam. Flutter: per tuft, its phase from its random size step and brightness (inst.w) and its place.
// Only literals the grass shaders already hold (0.25 0.5 0.75 2 1/128 1e-6): one more spills past c255 and the game refuses the shader.

// UNOFFICIAL 2026-11-04 (from Physically-Based-Reloaded; in place of Grass Collision, whose version is in VegasShaders-backups/
// before_grass_port_20261104) the player walking through the grass ([Shaders.Grass.Interaction], effects/Grass/Grass.cpp): There Stands
// The Grass NVSE's grass patch (features/GrassBend.cpp, Into-The-Rough), the same arithmetic. Each vertex is pushed away from the bend
// origin, horizontally, by (1 - distance / reach)^2 times the strength per unit of height times its own height above its tuft's root --
// so the root stays planted and every blade leans by the same angle. The distance is measured at the vertex itself (with 1 added under
// the root, as TSTG does, so it never divides by zero). rootWorld: the tuft's root, camera-relative; offset: the vertex from its root,
// after the wind; bend: xyz the origin (camera-relative), w 1 / the reach; push: the strength per unit of height. reached: the falloff.
float3 GrassBend(float3 rootWorld, float3 offset, float4 bend, float push, out float reached) {
    float2 away = rootWorld.xy + offset.xy - bend.xy;
    float inverse = rsqrt(dot(away, away) + 1.0f);
    float falloff = max(1.0f - bend.w / inverse, 0.0f);
    falloff *= falloff;
    reached = falloff;
    return float3(away * inverse * (falloff * push * max(offset.z, 0.0f)), 0.0f);
}

// UNOFFICIAL 2026-11-04 (from Physically-Based-Reloaded) [Shaders.Grass.LOD]: how much of a tuft shows by its size on screen (after
// Community Shaders' Grass Optimizations): its height in pixels is the mesh's radius times its scale over its depth, gone below
// MinPixels (raised for detailed meshes), full FadePixels above that. PBR NVR's per-draw and per-frame terms are folded per shape on
// the CPU (GrassShaders::SetBatchConstants): batch.x radius x pixels per unit / FadePixels, batch.y MinPixels x detail / FadePixels.
// 1 when off, or for a shape whose size did not come (batch.x 0).
float GrassLODFade(float4 rootClip, float height, float4 batch) {
    return batch.x > 0.0f ? saturate(batch.x * height / max(rootClip.w, 1.0f) - batch.y) : 1.0f;
}

// The camera-relative world position of a point from its clip position, for the card shaders' collision lookup (2026-10-20, user:
// 'blades of grass follow my player around in a bizarre fashion'). GetShadowWorldPos (Includes/Shadow.hlsl) rescales its result by
// clip.w / view z, which is 0 / 0 at the plane through the camera across the view -- exactly where the player's own feet are when he
// looks ahead -- so the tufts rooted round the player read the push grid at random places that changed with every step and turn. For
// this camera's projection (symmetric, RenderManager) view x, y are clip x, y times the inverse projection's diagonal and view z is
// clip w, with no division: exact at any depth, the same point elsewhere. Card shaders only (they declare both matrices).
#ifdef SHADOW_INVVIEW_REG
float3 GrassCameraRelative(float4 clip) {
    const float4 view = float4(clip.x * TESR_InvProjectionTransform._11, clip.y * TESR_InvProjectionTransform._22, clip.w, 1.0f);
    return mul(view, TESR_InvViewTransform).xyz;
}
#endif

// The whole move of a vertex in the field wind: the game's sway (swing as the game computes it, of the game's size) turned onto the
// wind direction, plus the gust lean along the wind and the flutter along it and half as much across, all times the game's sway size
// and the vertex weight (the game's: vertex alpha squared); z drops as the blade leans so it bends rather than stretches (height = the
// vertex's height above the tuft's root, as the grass collision does). windData = the game's c4 (xy its sway axis, z its size).
float3 GrassWindMove(float4 inst, float4 lanes, float clock, float swing, float4 windData, float weight, float height) {
    const float2 dir = lanes.xy * rsqrt(dot(lanes.xy, lanes.xy));
    const float gustPhase = clock * 0.25f;
    const float along = dot(inst.xy, lanes.xy);
    const float front = 0.5f + 0.5f * sin(gustPhase - along);
    const float patch = 0.75f + 0.25f * sin(gustPhase - 0.5f * (along + inst.y * lanes.x - inst.x * lanes.y));
    float2 flutter;
    sincos(2.0f * clock + inst.w + (inst.x - inst.y) * 0.0078125f, flutter.x, flutter.y);
    const float alongWind = swing * length(windData.xy) + lanes.z * front * front * patch + lanes.w * flutter.x;
    const float2 xy = (dir * alongWind + float2(-dir.y, dir.x) * (lanes.w * 0.5f * flutter.y)) * (windData.z * weight);
    const float h = max(height, 1e-6f);
    return float3(xy, -min(dot(xy, xy) * 0.5f / h, h));
}
