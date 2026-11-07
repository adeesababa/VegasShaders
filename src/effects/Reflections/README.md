# Reflections

Screen-space reflections on the ground (wet streets, dry sheen, interior floors, materials that reflect), with ray-traced reflections
on top where the RT core runs. Half-resolution trace, history and blur into the effect's own buffers, then a full-resolution composite
in the HDR chain (after Specular, before the fog). Started 2026-11-07 (this README did not exist before; the RT path's long history is
in docs/next/FORK-RT-REFLECTIONS.md).

## Frame (RenderBuffers)

1. The surroundings cube, the light grid's constants, the sky panorama.
2. **Trace** (technique 1, `Compute/ReflectionsTrace.frag` / the .fx) into Trace (Temporal on) or Temp. With RT
   (`[Shaders.Reflections.RT] Enabled`, the NVR DXVK fork, the RT core's helper): **RayGen** (`ReflectionsRayGen.frag`: the march,
   then Dir / Ray / March / MarchHit) -> the helper (`Compute/Rt/ReflectionsRT*.comp`) -> **Resolve** (`ReflectionsRTResolve.frag`:
   the hits shaded from the screen, the cube or the estimate; Spread = x the hit's distance share, y the hit distance).
   (2026-11-07) The hit normal the estimate is shaded with (sun N.L, lamps, grid / cube) is bent by the hit's own normal map in the
   textured variants (`ReflectionsRTTex*`) when the core's `[Main.Main.RayTracing] RealNormalMaps` is on (default, with RealTextures):
   bricks and panels off screen keep their bumps in mirrors. The shadow rays still leave from the flat triangle normal.
3. Smoothing, one of:
   - **the RT reflection denoiser** (`[Shaders.Reflections.RT] Denoiser`, default on with RT; core/RtReflDenoise.h,
     docs/next/RT-REFLECTION-DENOISER.md): 4 fork passes on frames the RT trace ran (native frames, not under DLSS RR), result copied
     into Buffer. Core 0 ours (ReBLUR-style: hit-point reprojection, long history, lobe-sized blur), core 1 AMD FidelityFX
     Reflections (ported, A/B). With it on, RayGen's glossy rays use decorrelated noise (the old pair sampled one line of each lobe).
   - **the old chain**: Temporal (technique 3: surface reprojection, 3 x 3 clamp, `Temporal` 0.85) -> Blur X / Y (2 / 4: depth-aware
     Gaussian, `BlurRadius` x roughness, narrower at RT contact). Always used without RT, in RenderScale rect frames, under DLSS RR
     (raw: no blur), and with `Denoiser false`.
4. The composite (technique 0) reads Buffer (water shaders read it next frame with RT Water).

## Settings
`[Shaders.Reflections.Main]` (the look and the SSR trace), `[Shaders.Reflections.RT]` (the RT trace, its fallbacks, and the denoiser:
`Denoiser`, `DenoiserCore`, `DenoiserHistory`, `DenoiserMirrorHistory`, `DenoiserBlur`, `DenoiserAntiLag`, `DenoiserClip`,
`DenoiserDebugView`; see defaults.toml for each).
- (2026-11-07) The RT rays' reach is FarRadius when the core holds the far ring: FarObjectLOD, FarLandLOD, or PTRealtime (which turns
  FarLandLOD on by itself, docs/next/RT-DETAIL-GAPS.md 7).
- (2026-11-07) Decals (`[Main.Main.RayTracing] Decals`, with AlphaTested: the ReflectionsRTAlpha* variants): the reflection rays see
  posters, cracks and blood (lifted 0.5 unit off their surface); the sun / key-lamp shadow ray at a hit passes them
  (ReflectionsRTBody.glsl RtProceedShadow). RT host scene `decals`.

## What correct / wrong looks like (the denoiser)
- At rest, a glossy floor's reflection settles within a second to a clean blur that is sharp where an object stands on the floor.
- Turning or walking: the reflection stays attached to the reflected object (no trail dragged along the floor); after a door opens or
  a light changes the reflection catches up within a few frames (anti-lag).
- Wrong: a reflection that smears along the floor when you move (the hit-point reprojection failing: `DenoiserDebugView 5` shows
  where it is used, green), a stuck ghost after a change (anti-lag), blotches that crawl (history too short: `DenoiserDebugView 3`).

## Tests
- RT test host scene `refldenoise` (`tools/nvr-rt-host/run.ps1 -Scene refldenoise`): the denoiser vs the old chain vs the exact glossy
  reflection, still and moving (no game).
- RT test host scene `normalmaps`: the RT trace's hit normal against the CPU's bend of a striped normal map (and flat with it off).
- Bench lists `rrdn-still`, `rrdn-turn` (bench/foundations/lists; run script bench/deep-roadmap/rtrefldn/run_rdn.sh).
