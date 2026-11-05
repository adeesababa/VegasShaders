# VolumetricLight - notes

The shader's own header (`src/hlsl/NewVegas/Effects/VolumetricLight.fx.hlsl`) describes the passes. This file holds what it does not.

## Where the light is applied

With `Froxels` on (the default) and the volumetric fog running, the fog applies the light grid in its own composite
(`VolumetricFog.fx.hlsl` FogGridLight, the Vulkan `VolumetricFogApply` under the DXVK fork); this effect's composites (technique 0 the
march, 8 CompositeGrid) draw only when the fog did not (`EffectChain.cpp` PreStep_VolumetricLight, `gridApplied`). A fix to how the light
is put on the picture has to go into both.

## The game's MSAA and see-through cutouts

Each pixel's air is read at the one depth the effects get (`TESR_DepthBuffer`). With the game's MSAA and transparency multisampling, a
cutout far off (a pylon's lattice, a bush) covers only part of each pixel's samples (alpha to coverage), but that depth is one sample's:
the nearest under the NVR DXVK fork, sample 0 under stock DXVK and native D3D9. The see-through part then took the air only up to the
cutout, so the cutout read as a solid shape in the light.

- 2026-11-04 (PR #5): this effect's D3D9 composites blended a surface pixel's air toward the whole view's by its exact sky share
  (`TESR_SkyCoverage`, `Includes/MsaaSkyEdge.hlsl` SkyShare). The fog's grid composite and the Vulkan composites did not, and with the
  defaults it is the fog that applies the light.
- 2026-11-05 `[Main.Main.Quality] MsaaEdgeFix` (on by default, live; off = as before): the composites here (`MsaaAirMix`): a surface pixel's
  sky share takes the whole view's air; a pixel that is sky by depth but only partly sky (sample-0 resolves) takes, for its surface share,
  the air up to the nearest surface 1-2 px round it. Ported to the Vulkan composites (`Compute/VolumetricLightComposite.frag`,
  `VolumetricLightCompositeGrid.frag`, `Compute/MsaaSkyEdge.glsl`; binding 3 `TESR_SkyCoverage`, `TESR_GameMultisample` at the end of
  the uniform block). The fog's FogGridLight blends the grid depth instead (see `src/effects/VolumetricFog/README.md`, section 7).
- `[Main.Main.Quality] MsaaEdgeDebugView 1` (test only): these composites show red where the depth says sky, green the exact sky share.

Bench 2026-11-05 (og-desert, `bench/foundations/lists/alphacut-vl.txt`, MaxDistance 15000 so the pylons lie inside the march): with MSAA 4
MsaaEdgeFix on vs off moved the pylon heads by +1.2 (fork) / +1.5 (native) luma, the rest of the frame |diff| 0.07 (noise); without MSAA on
vs off = noise. At the default MaxDistance 5000 the pylons there lie beyond the march and their air is the sky's already.

Left: a cutout against nearer ground (a bush in front of the land a few metres behind) is not helped: the shares only know sky samples, not
the depth of the land behind. The air between them differs little at that range.
