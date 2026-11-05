# Effect spec: LowHF

Status 2026-11-02 (branch claude/rw3-lens): `src/hlsl/NewVegas/Effects/LowHF.fx.hlsl` and `Compute/LowHF.frag` were written fresh from this spec; the settings, the constant layout and the push block are unchanged. Fixed from the old one: the blur is no longer diagonal (open question on the blur direction resolved as isotropic: it is now a cross of taps, centre plus one and two offsets along x and along y, nine reads, weights summing to 1). The shader itself has no divisions. The zero-limit and zero-base guards from section 6 are in the C++ (LowHF.cpp), which this rewrite does not touch, so they are still open.

Source of truth: src/hlsl/NewVegas/Effects/LowHF.fx.hlsl (D3D9), src/effects/LowHF.h and LowHF.cpp (C++ side), src/hlsl/NewVegas/Compute/LowHF.frag (Vulkan pass; compiled LowHF.spv sits beside it). Settings from resource/VegasShaders.defaults.toml. Frame order from src/core/ShaderManager.cpp.

## 1. Purpose

A low-health and low-stamina warning picture. When the player's health falls below a set fraction of its maximum, the whole screen loses colour (fades toward grey), gets a soft blur, and darkens toward the edges in a vignette that tightens as health drops. When stamina (called fatigue in the settings) falls below a set fraction, only the desaturation applies. At full health and stamina, or when the player is dead, the screen is untouched. The effect only changes the picture while the player is hurt or tired; it does not add anything.

## 2. When it runs

- Enabled by the Status key of the LowHF section (default off). When the effect is off, or its D3D9 shader is not loaded, nothing is drawn.
- Draw check: the effect draws only when the first component of its constant is not zero. That component is zero unless the player is alive and either health is below the health limit or stamina is below the fatigue limit (see sections 3 and 6). So with the player dead, or both values at or above their limits, the pass is skipped and costs nothing.
- Not limited to interiors or exteriors, night, or menus: the rule is only the player's vital values. The effect's own code does not turn it off underwater or in the first-person weapon view.
- Frame position: in the lens group of the post-process chain, after the Blood Lens and Water Lens and before the Dither Buster, SMAA, FXAA and Sharpening passes. It reads the frame's rendered picture as it stands after the earlier effects, and writes the next target of the chain (or the game target if this is the last effect to draw). Whether its input is HDR or already tonemapped is decided by the chain and is not checked by this effect (see section 9).
- Settings are re-read when the settings change. The health and stamina values are recomputed from live player values on each frame the effect is considered.

## 3. Inputs

Textures (D3D9 technique):
- Slot 0 (register s0), sampler name TESR_RenderedBuffer: the frame's rendered picture (the current chain input). Sampler state: minimum, magnification and mip filter all LINEAR; address mode CLAMP on U and V. This is the only texture. It is bound by name to the rendered buffer, not to the source buffer.

Vulkan pass textures and samplers:
- Descriptor set 1, binding 0: the rendered picture as a 2D sampled image (named t_rendered in the shader).
- Sampler: one sampler chosen by index through the push constant, using LINEAR filtering and CLAMP addressing (the LinearClamp sampler). No wrap sampler and no point sampler.

Constants:
- TESR_LowHFData: float4, looked up by name through the D3DX constant table (no fixed register is declared). Components, unitless unless stated:
  - x: desaturation amount, 0 to about 1 (0 = full colour, 1 = fully grey). Also the draw-check value: zero means not drawn.
  - y: blur offset in texture coordinates (a fraction of the picture width and height, applied to both the horizontal and vertical coordinate equally). Zero means no blur.
  - z: vignette exponent (zero means no vignette; higher values make a tighter, darker edge).
  - w: vignette floor, an offset added to the vignette factor before it is clamped (see section 6).
- No other constants are read by the shader.

Vulkan push constant block (C++ fills it; 32 bytes total):
- Offset 0, vec4 lowHFData: the same four components as TESR_LowHFData, in the same order.
- Offset 16, uint samplerIndex: the index of the sampler to use (LINEAR, CLAMP), read through the sampler table.
- Offsets 20, 24, 28: unused padding (three uints) so the block is a multiple of 16 bytes.

## 4. Settings

All keys are in section [_Shaders.LowHF.Main] unless noted. Each is read as a float.

- HealthLimit: float, default 0.5. Fraction of maximum health below which the health effect starts (health ratio = current health / base health). No range is declared. A value of zero or less makes the health ratio divide by zero (see section 6). Changes the threshold at which the screen starts to go grey and blurry.
- FatigueLimit: float, default 0.25. Fraction of maximum stamina below which the stamina desaturation starts (stamina ratio = current stamina / base stamina). No range is declared. Also the divisor in the stamina strength.
- LumaMultiplier: float, default 1.0. Scales the desaturation strength in both the health and the stamina case. At 1.0 the strongest level gives full grey.
- BlurMultiplier: float, default 0.2. Scales the blur offset. The offset applied is health strength times 0.01 times this value, so at the default the strongest blur offset is 0.002 of the picture size.
- VignetteMultiplier: float, default 1.0. Scales the vignette exponent. The exponent is health strength times 20 times this value.
- DarknessMultiplier: float, default 1.0. Scales the vignette floor. The floor is (1 minus health strength) times this value.
- [_Shaders.LowHF.Status] Enabled: boolean, default false. The on/off switch for the effect in the menu (the defaults comment reads "Screen and sound effects when the player health is low").
- [_Main.LowHFSound.Main] (low-health / fatigue sound keys, an Oblivion Reloaded leftover that no code read) was removed on 2026-11-05.

Definitions used below:
- Health strength = 1 minus (health ratio / HealthLimit). It is 0 at the threshold and rises to 1 at zero health (with the default limit, when health is zero).
- Stamina strength = 1 minus (stamina ratio / FatigueLimit). It is 0 at the stamina threshold and 1 at zero stamina.

## 5. Passes and outputs

- Technique index 0 is the only technique. The D3D9 file declares it without a name (one pass). The C++ always asks for index 0; any other index draws nothing.
- Pass: one full-screen pass. Vertex stage passes the position and texture coordinate through unchanged (full-screen quad, as the other effects use). Pixel stage: the shader described in section 6, output colour alpha set to 1.
- Output: one colour target, the target the chain gives this effect. Same size and format as the input picture. The effect overwrites the target; it does not blend with what was there.
- Vulkan: the pass is named LowHF (file LowHF.spv beside LowHF.frag). It is selected only when the Vegas Shaders DXVK extension is present and NvrDxvkPasses is on, and only for technique index 0. If the Vulkan pass fails, the effect falls back to its D3D9 shader for the rest of the session (logged).
- The effect has no render target or texture of its own; it reads and writes only the chain's targets.

## 6. Behaviour details

Health and stamina values (computed on the C++ side, before drawing):
- Health ratio = current health / base health; stamina ratio = current stamina / base stamina, both read from the player actor.
- If the player is not alive, all four components are zero and nothing draws.
- If health ratio is below HealthLimit: desaturation = health strength times LumaMultiplier; blur offset = health strength times 0.01 times BlurMultiplier; vignette exponent = health strength times 20 times VignetteMultiplier; vignette floor = (1 minus health strength) times DarknessMultiplier. At the threshold: no desaturation, no blur, no vignette, floor equal to DarknessMultiplier. At zero health: full desaturation, the strongest blur, the tightest vignette, and a floor of zero.
- Stamina applies only when the health case did not set the desaturation, so the two never combine. If stamina ratio is below FatigueLimit, desaturation = stamina strength times LumaMultiplier and the other three components stay zero (no blur, no vignette).
- Edge case: if health strength is exactly zero (health exactly at HealthLimit), the desaturation is zero, so the stamina case may apply in that frame.

Pixel shader order (the same in D3D9 and Vulkan; each step works on the colour the previous step produced):
1. Start with the picture colour at the pixel.
2. (New shader, 2026-11-02) Blur, when the blur offset is greater than zero: nine taps, a cross around the pixel (centre weight 0.2; one offset along x and along y at 0.15 each; two offsets along x and y at 0.05 each). Isotropic, not diagonal. The original five-sample diagonal description below is what was replaced.
   Original: take five samples and average them. The samples are at the pixel, at plus one offset, at plus two offsets, at minus one offset, and at minus two offsets. The offset is added equally to the horizontal and the vertical texture coordinate, so the blur runs along the diagonal, not horizontally or vertically. This is the behaviour as written; whether a diagonal blur was intended is an open question (section 9). Blur happens before desaturation, so the grey comes from the blurred colour.
3. Desaturate, when the desaturation amount is greater than zero: mix the colour toward its luminance, using the weights 0.2125 (red), 0.7154 (green) and 0.0721 (blue), close to Rec.709. Amount 0 leaves the colour unchanged, amount 1 gives pure grey. The mix is linear and not clamped, so an amount above 1 (possible with LumaMultiplier above 1) pushes the colour past grey. Keep it unclamped as written.
4. Vignette, when the exponent is greater than zero: take the distance from the screen centre in texture coordinates, squared (centre minus 0.5 on each axis, then dot product). The vignette factor is one minus that squared distance: about 1 at the centre, about 0.5 at the corners. The multiplier is the vignette factor raised to the exponent (of its absolute value), plus the floor, clamped to 0 to 1. Multiply the colour by the multiplier. The centre stays bright, the edges darken; a high exponent confines the dark to the very edge, and a low exponent darkens more of the screen. With a floor above zero, the edges keep that fraction of their brightness.
5. Output the colour with alpha 1.

Other behaviour:
- Sky and depth: the effect reads no depth and no sky data. Sky pixels are treated like all others.
- First-person weapon and jitter: the effect reads no weapon layer and no jitter offset; it acts on the finished picture.
- RenderScale and render rect: the effect does not transform coordinates for the render rectangle or the render scale. Texture coordinates run 0 to 1 across the whole input target, and blur offsets are in those same units. Open question whether this matters at RenderScale below 1.
- Reversed depth: not used.
- HDR versus LDR: the shader does no tonemapping and no clamping of its input. Values above 1 pass through the blur and the desaturation unchanged.
- NaN and infinity: no guard. A zero or negative HealthLimit divides by zero in the health ratio and gives infinite or NaN strengths. A zero base health or base stamina has the same problem. A guard should clamp the limits to a small positive minimum and treat a zero base value sensibly.
- Things that would look wrong if missed: a blur that is horizontal or vertical only; desaturation that also darkens the centre; the vignette applied when the exponent is zero; the stamina case blurring or darkening; the effect drawing while the player is dead; the health threshold reversed (grey at full health).

## 7. Vulkan path

- Files: src/hlsl/NewVegas/Compute/LowHF.frag (source) and LowHF.spv (compiled, the one loaded). The shader includes the shared Common shader file of the compute folder, and uses its sampler helper to pick the sampler by index.
- Selection: src/effects/LowHF.cpp calls the Vulkan pass first when Vegas Shaders DXVK is present, NvrDxvkPasses ([_Main.Main.Performance] NvrDxvkPasses, true in the defaults file) is on, and the effect is not named in the keep-on-D3D9 list (NvrDxvkKeepD3D9). It does so only for technique index 0. It uses the shared full-screen Vulkan pass helper with one colour target, the rendered picture as its only texture, and one sampler (LinearClamp, sampler push offset 16).
- Push constant layout (32 bytes): offset 0 lowHFData (vec4, the four components of section 3); offset 16 samplerIndex (uint). The C++ fills lowHFData from the same constant the D3D9 path uses, and sets samplerIndex to the LinearClamp sampler's index.
- Descriptor layout: set 1, binding 0: sampled 2D image (the rendered picture). The sampler is referenced by the push index, not by its own binding.
- Vertex stage and full-screen triangle: the shared full-screen pass setup used by the other compute-folder passes.
- The Vulkan shader performs the same five steps as section 6, in the same order, with the same weights and clamps. No difference in the sampling coordinate was found between the two paths.
- Fallback: if the Vulkan pass cannot be created or fails, the effect falls back to the D3D9 technique for the session and logs once.

## 8. Look and cost

- Correct: at full health and full stamina the picture is unchanged. Below the health threshold the picture loses colour, gets a slight soft blur, and the edges darken; the closer to zero health, the greyer, blurrier and darker the edges, with the centre kept bright. Stamina below its threshold gives only the grey tint, with no blur and no edge darkening. The change grows smoothly as health falls and starts from zero at the threshold, so there is no popping there.
- Typical wrong results: the screen goes grey at full health; the blur smears diagonally in a way that reads like motion (see section 6 for the intended behaviour and section 9 for the open question); the whole screen darkens instead of only the edges; the vignette has a hard edge; the stamina case also blurs; the effect runs during death or with the effect switched off.
- Cost class: cheap. One full-screen pass: five texture reads when the blur is on, one otherwise, plus a few arithmetic operations. Skipped entirely when the draw check is zero.

## 9. Open questions

- Blur direction: the offset is added to both texture axes equally, so the blur runs along the diagonal. Confirm whether this is intended or whether a single-axis blur was meant. The writer should match the behaviour the owner decides on.
- [_Main.LowHFSound.Main] (low-health / fatigue sound keys, an Oblivion Reloaded leftover that no code read) was removed on 2026-11-05.
- Ranges: no range is declared or clamped for HealthLimit, FatigueLimit, LumaMultiplier, BlurMultiplier, VignetteMultiplier or DarknessMultiplier. The safe range for each is not known.
- Constant update timing: settings are re-read when the settings-changed flag is set. The per-frame timing of the health and stamina update was not traced exactly; confirm it runs on every frame the effect is considered.
- Pre- or post-tonemapping: the effect sits in the lens group after motion blur. Whether its input is HDR or tonemapped, and what RenderPreTonemapping does to it, was not traced.
- Render rect and RenderScale: the effect does not use the render rectangle. Whether a scaled input needs the blur offset converted to the target's pixel size is not confirmed.
- Vulkan sampler table: the exact index value of the LinearClamp sampler is set by the extension at run time; the spec gives only the sampling mode.
- Dead player: the code treats a dead player as zero strength. Confirm there is no case where the player object is missing (the check is on the alive flag only).
