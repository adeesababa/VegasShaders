# VolumetricClouds - notes

The shader's own header (`src/hlsl/NewVegas/Effects/VolumetricClouds.fx.hlsl`) describes the passes. This file holds what the header does
not: how the composite tells sky from surface, and the game's MSAA.

## Sky or surface: the composite

The composite (technique 0, full resolution) puts the clouds over a pixel only where it is sky. Without the game's MSAA that is the depth
alone: `TESR_DepthBuffer` at the far plane. An alpha-tested cutout (a pylon's lattice, foliage) writes depth only where its texture is kept,
so the holes read as sky and get the clouds; that case is exact.

With the game's MSAA (`iMultiSample` above 1; since 1.0.4 it is no longer blocked) and transparency multisampling
(`bTransparencyMultisampling`, the TMS shaders), the game draws cutouts with alpha to coverage: a pixel where the texture's alpha is 0.4
gets about 40 % of its samples. Far off, where the texture's smaller mips average the holes into the bars, the whole outline of a lattice or
a bush is such mixed pixels. One depth per pixel cannot describe that: the NVR DXVK fork resolves the nearest sample (the whole mixed area
reads as surface), stock DXVK and native D3D9 sample 0 (each pixel surface or sky by chance).

- 2026-11-04 (PR #5): `TESR_SkyCoverage` (core/SkyCoverage.h): a far-plane quad drawn against the multisampled depth before the resolve
  gives each pixel's exact sky share. The composite gave the clouds over that share, but only to pixels that are surface by depth, and it
  took the colour of the sky behind from pixels 1-2 px away that are sky by depth, else the pixel's own colour (part bar).
- 2026-11-05 `[Main.Main.Quality] MsaaEdgeFix` (on by default, live; off = the 2026-11-04 behaviour): every pixel by its exact share
  (`Includes/MsaaSkyEdge.hlsl`): all sky (share >= 0.998) the clouds as before, none (<= 0.002) the cloud shadow as before, mixed the clouds
  over the share only, also for pixels that are sky by depth (sample-0 resolves gave them the clouds whole, over the bars). The sky behind
  comes from pixels all sky by their share, 1, 2, 4 and 8 px away along the axes (a lattice far off is all mixed pixels: within 2 px there
  was often none). Only while this frame's shares were made (`TESR_GameMultisample.y`, set by SkyCoverage::Draw).
- `[Main.Main.Quality] MsaaEdgeDebugView 1` (test only): the composite shows red where the depth says sky, green the exact sky share.
- The Vulkan composite (`Compute/VolumetricCloudsComposite.frag`, only with `[Main.Main.Performance] NvrDxvkClouds`) had no sky shares at
  all; since 2026-11-05 it has MsaaEdgeFix (binding 5 `TESR_SkyCoverage`, `TESR_GameMultisample` at the end of the uniform block), not
  the 2026-11-04 form.

Bench 2026-11-05 (og-desert, cloudy, lattice pylon heads against a cloud band, `bench/foundations/lists/alphacut-fix.txt`): with MSAA 4
the bars under the cloud brightened +56 (fork) / +64 (native) luma with the 2026-11-04 composite, +44 / +54 with MsaaEdgeFix (the bars
keep their dark lattice instead of turning cream). Without MSAA on vs off differ only by noise (|diff| 0.12).

Left: a mixed pixel takes no cloud shadow on its surface part (as before).
