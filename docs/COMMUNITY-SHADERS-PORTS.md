# Community Shaders ports: what came from where, and under which licence

Written 2026-10-19 with the five features below; LOD Blending (the sixth) added the same day. Kept so that anyone can check, file by file, that nothing was taken from a
closed or unlicensed source.

## The short version

- Every piece of outside code used here is **GPL-3.0** (Community Shaders) or **GPL-3.0-or-later** (Conor's NVR fork).
  This project is GPL-3.0-or-later with the same Modding and Linking exceptions, so the combination is allowed. The
  combined work is distributed as GPL-3.0 with its source public (this repository).
- Each ported file and function says at its top where it came from (repository, path, commit) and keeps the original
  credit. Nothing was relicensed or stripped of a notice.
- Parts written for this build (the land-height reader and its inflate, the DirectX 9 cloud shadow pass, the grass
  push texture and its CPU side, the sky-visibility channel for reflections, the composite plumbing) are this project's
  own code, under its licence.
- Not used, on purpose: the Community Shaders logo and icons (no licence for redistribution), their glints shader (origin
  unclear), any vendor SDK (DLSS, FSR, Streamline), and their shipped heightmap textures (we read the player's own game
  files at run time instead, so no game-derived data is distributed).

## Sources checked (2026-10-19)

| Source | Commit read | Licence, as checked |
|---|---|---|
| github.com/community-shaders/skyrim-community-shaders (branch `dev`) | 3f9f72d | `COPYING.txt` GPL-3.0; README "License: GPL-3.0 WITH Modding Exception AND GPL-3.0 Linking Exception"; `EXCEPTIONS.md`. Per their README, files without their own header are GPL-3.0 with the exceptions; `package/SKSE/Plugins/CommunityShaders/Licenses/THIRD_PARTY.md` lists the exceptions to that (none of the files below are on it, except SebH's spherical harmonics, MIT, which is not used here). |
| github.com/northaxosky/fallout4-community-shaders | e499056 | GPL-3.0-or-later + the same exceptions (read for the heightmap workflow only; no code taken). |
| github.com/mrfury97/Conor-sClaudeHell-NVR-SAL-Fork, branches `testbed` and `claude/terrain-variation` | 9000473d, 6043062a, bae95aef; 66653eea, d07ff4b9 | A fork of macrimmon12-tech/TESReloaded10. Its `COPYING.txt` is the GPL-3.0 text and its `License.md` is byte-for-byte this project's (GPL-3.0-or-later with the same section 7 terms). GitHub shows "Other" only because of those extra terms. |

Files of the Community Shaders features that were read (none carries its own licence header, none is in THIRD_PARTY.md,
so all are GPL-3.0 with the exceptions):
`features/Terrain Shadows/Shaders/TerrainShadows/{TerrainShadows.hlsli,ShadowUpdate.cs.hlsl}`, `features/Terrain Shadows/textures/heightmaps/readme.txt`,
`features/Cloud Shadows/Shaders/CloudShadows/CloudShadows.hlsli`, `src/Features/CloudShadows.cpp`,
`features/Grass Collision/Shaders/GrassCollision/{GrassCollision.hlsli,CollisionUpdateCS.hlsl}`,
`features/Terrain Variation/Shaders/TerrainVariation/TerrainVariation.hlsli`,
`features/Skylighting/Shaders/Skylighting/Skylighting.hlsli`, `src/Features/Skylighting.{h,cpp}` (the MinSpecularVisibility default),
`features/LOD Blending/Shaders/Features/LODBlending.ini`, `src/Features/LODBlending.{h,cpp}`, and the `LOD_BLENDING` lines of
`package/Shaders/Lighting.hlsl` (the brightness and gamma on the LOD land, DisableTerrainVertexColors).

## Feature by feature

### Mountain shadows ([Shaders.ShadowsExteriors.TerrainShadows])
- `src/effects/TerrainShadows.{h,cpp}`: **Conor's code** (his `TerrainShadows.{h,cpp}` at 9000473d), itself after Community
  Shaders' Terrain Shadows. Kept: the xLODGen height-map reader, the worker-thread sweep toward the sun, the strip upload.
  Changed: a fallback to the land read from the plugins; the load request built on the main thread.
- `src/effects/LandHeights.{h,cpp}`: **this build's own code.** Reads the LAND records of a worldspace from the player's own
  plugins in load order (format from UESP / xEdit documentation) and a small zlib inflate written from RFC 1950/1951.
- `Effects/SunFar.fx.hlsl` `TerrainShadow()`, `TerrainShadowCoverage()`, `SkyIrradiance()` and the share formula: **Conor's**
  (his `ShadowsExteriors.fx.hlsl` at 6043062a). The separate pass, the composite hooks and the single-lookup cascade test are
  this build's.
- `ShadowsExteriorEffect::UpdateLightColors`: **Conor's** (6043062a).

### Cloud shadows ([Shaders.CloudShadows.Main])
- **This build's own code**, after the idea of Community Shaders' Cloud Shadows (the cover of the game's cloud layers where
  the light from a point meets the cloud layer, dimming only the sun). CS captures the layers into a cube map during the
  sky's reflection pass (D3D11); here the sky's four cloud textures are read directly on a flat layer above the camera.
  The 2 km height default is CS's `CloudHeight`.

### Grass collision ([Shaders.Grass.Collision])
- **This build's own code**, after the idea of Community Shaders' Grass Collision (actors flatten the grass round them, the
  grass vertex shader bends away, a spring brings it back). CS uses a compute shader and a constant buffer; here the CPU
  keeps a world-locked push grid and the grass vertex shaders read it as a vertex texture.

### Less repeating ground ([Shaders.Terrain.Variation])
- `Shaders/Includes/TerrainVariation.hlsl`: **Conor's port** (66653eea, d07ff4b9) of Community Shaders' Terrain Variation:
  the skewed lattice, the hash, the corner ranking and fades, the height-weighted blend (constants identical to CS's
  `TerrainVariation.hlsli`). After Deliot and Heitz, "Procedural Stochastic Textures by Tiling and Blending" (2019).
  Changed: the taps use this build's explicit gradients and empty-layer skip; StartDistance; register c95.

### Sky reflections under cover ([Shaders.AmbientOcclusion.Skylighting] Reflections)
- **This build's own code**: Skylighting v2's visibility kept in a spare channel and applied to the Specular effect's sky
  reflection and to the sky fallback of the wet-ground reflections. The blend `lerp(MinReflection, 1, visibility)` and its
  0.1 default follow Community Shaders' Skylighting `MixSpecular` / `MinSpecularVisibility`.

### Far land matching ([Shaders.Terrain.LODBlending])
- **This build's own code**, after Community Shaders' LOD Blending: the far land's colour taken to a gamma and times a
  brightness (`pow(abs(color), gamma) * brightness`, as their LOD land does), here in `TerrainLODTemplate.hlsl` and the
  near/far fade (`TerrainFadeTemplate.hlsl`), and the near land without its vertex colours (their
  `DisableTerrainVertexColors`). Their LOD-object and snow knobs are not ported (New Vegas draws far objects with the
  ordinary object shaders, so there is no LOD-object shader to adjust).

## Optimisation pass (2026-10-19, same picture)
Done after the ports, none of it changing what is drawn (checked in game against the first build: the same pixels change by
the same amount): Skylighting's target keeps two channels unless the sky reflections are on (3.5 MB of video memory at
1280x720); the mountain shadows keep the ground height in a texture of its own, written once a map, so each sweep packs and
uploads half the bytes (about 11 MB less memory in the game's process at the default size); the SunFar pass reads the normals
only past its early-out; grass beyond the push texture or where no one pushes skips the read and the second transform; the
push texture is only sampled near the pushed cells. None of these moves a frame time measurably; they save memory, uploads
and a little CPU. Tried and dropped: skipping the composites' new texture reads with a dynamic branch while the features are
off. It measured slower (the fog composite 0.47 -> 0.52-0.53 ms, two launches each), and the plain read costs nothing
measurable with them off (0.47, the same as P81's shader without it).

## Credit lines for release notes

- "Community Shaders team and contributors (github.com/community-shaders/skyrim-community-shaders, GPL-3.0): Terrain Shadows,
  Cloud Shadows, Grass Collision, Terrain Variation, LOD Blending and Skylighting, adapted for DirectX 9."
- "Conor (mrfury97, github.com/mrfury97/Conor-sClaudeHell-NVR-SAL-Fork, GPL-3.0-or-later): the terrain shadows' height-map
  sweep and share composite, and the terrain variation port."
- "Terrain variation after Deliot and Heitz 2019."
- "Bottled Shaders (github.com/InTheBottle/Bottled-Shaders, GPL-3.0) via mrfury97/VegasShaders@9107a20: Screen-Space Shadows,
  ported one to one for deferred sun lighting (`ScreenSpaceShadows.h`, `Compute/ScreenSpaceShadows.comp`, `BendDispatch.h`);
  the raymarch is Bend Studio's screen-space shadows, Copyright 2023 Sony Interactive Entertainment (Apache-2.0)."
