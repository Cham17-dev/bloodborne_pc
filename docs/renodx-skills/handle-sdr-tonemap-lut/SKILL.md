---
name: handle-sdr-tonemap-lut
description: "RenoDX HLSL/Slang shader workflow for SDR tonemap, hard clip, LUT, color grade, and HDR bridge changes. Use when editing game shaders for ToneMapPass/RenoDRT, PsychoV17, vanilla midgray/slope or inflection matching, UpgradeToneMap delta, SDR LUT bridges, N2 max-channel reconstruction, gamut compression, internal/user LUTs, or shader-side tonemap/LUT replacements."
argument-hint: "game shader and goal, e.g. audit LUT bridge, add PsychoV17, or fix hard-clip ToneMapPass"
---

# Handle SDR Tonemap + Optional LUT

Use this skill for RenoDX **shader-side** tonemap/LUT work in HLSL or Slang. Keep the workflow focused on shader changes: finding the right color signal, preserving the vanilla SDR appearance, and choosing a shader bridge into HDR.

Treat the visible SDR look as a pipeline, not just a curve:

```text
scene / post exposure / bloom / vignette / fade
-> SDR tonemap or hard clip
-> optional user LUT
-> internal/generated LUT or color grade
-> blend masks / strength / scaling
-> output encode
```

## Boundaries

- Focus on HLSL/Slang shader changes and shared shader includes.
- Read `shared.h` only to understand shader injection constants/macros used by the shader.
- Put nontrivial reusable shader logic in the game's `common.hlsl`, `common.hlsli`, or an existing nearby purpose-specific `.hlsli` include when that fits the folder pattern.
- Do not turn this into addon UI, C++ plumbing, resource upgrade, CMake, or release workflow guidance.
- If the shader fix requires addon/resource/C++ work, report that as an external requirement instead of expanding this workflow.

## Shader rules

- Keep changes local to the game/shader being worked on.
- RenoDRT is `renodx::draw::ToneMapPass(...)`.
- Prefer current `ToneMapPass` methods used in the repo such as Neutwo or Reinhard. Do not recommend Hermite or Daniele as new strategy choices.
- Do not extract helpers for local control flow, one-off math, basic field access, or short condition checks.
- Do move substantial tonemap/LUT bridge functions into a game-local common include instead of pasting large logic blocks into every original shader.
- When modifying decompiled/original shaders, keep edits minimal: capture the needed signal, call a common include helper or small scoped block, then assign the output.
- Clearly mark custom snippets and preserve vanilla logic by leaving it intact or commenting out replaced lines where practical; avoid silently deleting vanilla code.
- Prefer lightly injected snippets in original shaders over broad rewrites.

## Output expected from this skill

Produce these sections:

1. **Detected shader pipeline** - capture point, tonemap/clip, LUTs, encodings, masks, and output.
2. **Shader strategy** - primary HLSL/Slang approach and why.
3. **Fallback/legacy shader path** - what to preserve if replacing is risky.
4. **Shader edit plan** - exact shader files, include files, and injected constants used.
5. **Visual/math validation** - curve, slope, LUT, and screenshot checks.

## Step 1: Discover the shader pipeline

Inspect the relevant shader(s) and nearby shared HLSL/Slang includes. Answer:

| Area | What to identify |
|---|---|
| Local includes | Existing `common.hlsl`, `common.hlsli`, or purpose-specific `.hlsli` files that can hold helper logic. |
| Capture point | Last useful linear scene value before SDR clamp/tonemap/LUT. |
| Presentation effects | Exposure, fades, bloom, vignette, hero lights, grain, or clip effects already inside the shader. |
| Tonemap | Analytic curve, hard clip/saturate, black-box shader math, or none. |
| LUTs | Internal LUT, generated LUT, optional user LUT, chained LUTs, or custom LUT draw count. |
| Encoding | Linear, sRGB, gamma 2.0/2.2/2.4, packed 2D LUT, or 3D LUT. |
| Sampling | Trilinear, tetrahedral, manual 2D strip, or hardware sample. |
| Blending | Strength scalar, vanilla constant, per-pixel blend mask, or multiple LUT paths. |

## Step 2: Choose the shader reference scope

Use the narrowest reference scope that preserves the intended look.

| Scope | Use when |
|---|---|
| `tonemap-only` | The LUT is unrelated or should remain a separate post-grade bridge. |
| `tonemap-plus-lut` | Final SDR look is defined by tonemap plus LUT. |
| `full-sdr-look` | Effects such as fades, vignette, bloom, masks, or exposure are part of shader presentation. |
| `hard-clip-sdr` | There is no true tonemap; vanilla is saturate/clip plus optional LUT. |
| `unknown-black-box` | No analytic model is reliable; sample or measure shader behavior. |

## Step 3: Identify shader signals

Prefer a linear scene capture before SDR clipping, but keep intentional presentation scaling if it belongs to the vanilla look.

| Signal | Meaning |
|---|---|
| `untonemapped` | Linear scene/HDR source that will feed `ToneMapPass` or PsychoV. |
| `neutral_sdr` | Vanilla ungraded SDR baseline, such as hard clip, neutral SDR tonemap, or neutral LUT input. |
| `graded_sdr` | Vanilla SDR after LUT/color grade/masks. |

For hard-clip/no-tonemap shaders, do not omit `neutral_sdr` if vanilla neutral is `saturate(untonemapped)` or another custom proxy. The two-argument `ToneMapPass(untonemapped, graded_sdr)` defaults the neutral path to RenoDRT neutral SDR, which may be wrong for hard-clip vanilla.

In original/decompiled shaders, prefer a small injection shape:

1. capture `untonemapped`, `neutral_sdr`, or `graded_sdr` near the vanilla signal;
2. leave the vanilla computation readable nearby;
3. replace only the final color assignment or narrow branch with the RenoDX call.

## Step 4: Choose the shader HDR strategy

| Strategy | Status | Use when |
|---|---|---|
| `ToneMapPass(untonemapped)` | Preferred | No SDR LUT/grade needs to be preserved. |
| `ToneMapPass(untonemapped, graded_sdr)` | Preferred | A trustworthy graded SDR result exists and default RenoDRT neutral SDR is acceptable. |
| `ToneMapPass(untonemapped, graded_sdr, neutral_sdr)` | Preferred | Hard clip/no-tonemap or custom neutral SDR baseline. |
| `direct-psychov17` | Preferred for PsychoV | User requests PsychoV17 or observer tonemap; handle LUT bridge after PsychoV unless final SDR look is the reference. |
| `extended-vanilla` | Situational | Preserve an analytic vanilla curve shape above SDR range. |
| `tonemap::config::Apply + LUT` | Legacy/compat | One-shot tonemap+LUT texture helper is already wired or low-risk. |
| `UpgradeToneMap delta` | Legacy | Existing luminance-delta bridge must be preserved; avoid as a new default. |

### One-shot tonemap + LUT helper

`renodx::tonemap::config::Apply(color_input, tm_config, lut_config, lut_texture)` exists in `src/shaders/tonemap.hlsl`. It applies HDR and SDR tonemaps, samples a LUT, then uses `UpgradeToneMap` for non-vanilla types. Treat it as compatibility shader code, not the modern default.

### Modern graded ToneMapPass path

The preferred graded shader path is in `src/shaders/draw.hlsl`:

- `renodx::draw::ComputeUntonemappedGraded(untonemapped, graded_sdr, neutral_sdr)`
- `renodx::draw::ToneMapPass(untonemapped, graded_sdr, neutral_sdr)`

This still uses delta reconstruction internally, but keeps the bridge localized and feeds the result through the current `ToneMapPass` configuration.

## Step 5: Handle inflection and curve matching

Use inflection matching when the vanilla tonemap has a meaningful knee/pivot and the goal is to preserve local contrast around that knee rather than force output midgray. Do not conflate inflection matching with output-midgray matching; they are different anchors.

| Inflection task | What to do |
|---|---|
| Identify the scalar curve | Find the vanilla scalar curve used for matching, often Hable/Uncharted2-style or another filmic curve. |
| Reuse existing math | Prefer existing inverse, derivative, second derivative, or `Find*Root` functions if they already exist in shader code. |
| Choose the pivot | Prefer analytic second-derivative inflection; otherwise use a documented numeric solve. |
| Compute anchor | `anchor_in = x_inflection`, `anchor_out = V(anchor_in)`. |
| Compute slope | Prefer analytic `V'(anchor_in)`; use finite-difference only when no derivative exists. |
| Convert to PsychoV cone | `cone = anchor_in * V'(anchor_in) / max(anchor_out, epsilon)`. |
| Verify perceptually | Compare in viewer/output space, not only scene-linear input space. |

If the curve has a black offset, flare, or toe crush, include that behavior while measuring vanilla slope. Do not force the HDR replacement or PsychoV path to reproduce unwanted crush unless explicitly requested.

## Step 6: PsychoV17 shader matching options

Only add PsychoV17 when requested or when the game already exposes it. Use `renodx::tonemap::psychov::psychotm_test17(...)`.

| Option | Meaning |
|---|---|
| `anchor-none` | Use PsychoV defaults. |
| `anchor-scene-midgray` | Anchor at scene input `x = 0.18`. |
| `anchor-output-midgray` | Solve vanilla `V(x) = 0.18`; this is usually the correct midgray output match. |
| `anchor-inflection` | Match around the vanilla knee/inflection. |
| `anchor-sampled` | Use measured black-box samples when no analytic vanilla model exists. |
| `slope-off` | Do not match vanilla local slope. |
| `slope-delta` | Use finite-difference slope; acceptable if no derivative exists. |
| `slope-analytic` | Prefer analytic derivative when available. |

For PsychoV cone matching, compute the baseline cone response from the vanilla reference as:

```text
cone = x * V'(x) / V(x)
```

where `x` is the anchor input and `V(x)` is the anchor output.

## Step 7: LUT shader bridge options

Classify LUT handling before choosing a bridge.

| Option | Meaning |
|---|---|
| `no-lut` | No LUT preservation. |
| `sample-existing-lut` | Use the game's actual LUT result as `graded_sdr`. |
| `internal-lut` | Preserve generated/internal game LUT. |
| `user-lut` | Preserve optional user LUT, usually controlled by constants. |
| `multi-lut` | Multiple LUT draws, chained LUTs, or tracked custom LUT count. |
| `lut-blend-mask` | Preserve per-pixel masks or blend factors. |
| `lut-strength` | Respect shader-side LUT strength. |
| `lut-scaling` | Respect shader-side HDR reconstruction scaling. |
| `trilinear` | Standard LUT sampling. |
| `tetrahedral` | Use when vanilla or existing mod path expects tetrahedral sampling. |
| `manual-2d-strip` | Preserve packed 2D LUT sampling when direct `renodx::lut::Sample` is not equivalent. |

## Step 8: SDR/HDR reconstruction shader options

| Bridge | Status | Use when |
|---|---|---|
| `direct-sdr` | Simple | SDR values are bounded and gamut-safe. |
| `neutral-sdr-baseline` | Preferred | `ToneMapPass(untonemapped, graded_sdr, neutral_sdr)` can preserve the grade. |
| `n2-max-channel` | Preferred | HDR must be compressed through an SDR LUT while preserving peak/max-channel ratio. |
| `luminance-scale` | Situational | Luminance-preserving reconstruction is enough. |
| `per-channel-scale` | Situational | Per-channel preservation is desired and hue shifts are acceptable. |
| `adaptive-d65-gamut` | Preferred for saturated HDR | Compress/decompress BT.709 against an adaptive D65 state around the LUT. |
| `gamma-gamut` | Legacy/situational | Gamma-domain compression/decompression already exists. |
| `smooth-clamp` | Situational | Hable/Uncharted-style SDR LUT bridge needs smooth clipping behavior. |
| `UpgradeToneMap-delta` | Legacy | Preserve old luminance-delta behavior; avoid new default if better bridge exists. |

## Step 9: Scratch graph and image analysis

Use scratch graphs/images only to guide shader math. Keep artifacts under `.tmp/` or another temporary path and do not add them to the repo unless requested.

Useful graph patterns for this workflow:

| Artifact type | Use |
|---|---|
| Anchor comparison | Compare output-midgray, inflection, and sampled anchors. |
| Viewer-space stop plot | Plot `log2(output / 0.18)` to compare perceived output shifts. |
| Gain/loss plot | Compare candidate output against vanilla output across stops. |
| Slope/derivative plot | Verify local slope or slope ratio near midgray/knee/inflection. |
| Gamma/EOTF check | Verify whether the shader path is linear, sRGB, gamma 2.2, gamma 2.4, or BT.1886-like. |
| LUT bridge image | Compare neutral LUT, strong LUT, user LUT on/off, and saturated emissive colors. |

Prefer compact scripts and clearly named `.tmp/` files. Do not make scratch artifacts part of the shader change unless the user asks.

## Committed RenoDX shader examples

Use these as references, not as templates to copy blindly.

| Game | Useful shader pattern |
|---|---|
| `starfield` | PsychoV17 vanilla output-midgray/slope matching; LUT strength/scaling/sampling/blend mask; N2-style LUT bridge. |
| `gtav-enhanced` | Hable/Uncharted analytic derivative and inflection anchor for PsychoV17; `ToneMapPass`/Neutwo for RenoDRT. |
| `batmanak` | Uncharted2 extended/extrapolate path, LUT strength/sampling, smooth clamp. |
| `wobblylife` | SDR LUT Bridge shader paths; max-channel N2 for main LUT; gamma-gamut or Adaptive D65 gamut bridge for custom LUT draws. |
| `hollowknight-silksong` | Hard clip/no-tonemap style path; explicit `neutral_sdr` and `graded_sdr` can feed `ToneMapPass`. |
| `valheim` | PQ LUT encode/decode, neutral SDR LUT sampling, and legacy `UpgradeToneMap` bridge. |
| `acecombat7` | LUT builder and output LUT shaders with SRGB/PQ sampling plus `UpgradeToneMap` compatibility. |
| `angerfoot` | Unity-style LUT shaper, generated LUT variants, and tetrahedral LUT sampling across post shaders. |
| `xfextremeformula` | ARRI/log LUT input, LUT strength/scaling/tetrahedral controls, and graded `ToneMapPass` bridge. |

## Shader validation checklist

- Compare vanilla SDR and custom HDR around black, midgray, highlight rolloff, and saturated colors.
- For inflection matching, verify the chosen pivot, derivative source, and output-space behavior.
- For PsychoV matching, verify the anchor (`V(x)=0.18` or inflection) and slope source in viewer/output space when possible.
- For LUT bridges, test neutral LUT, strong LUT, user LUT on/off, and saturated emissive colors.
- Check UI/HUD/video/menu shader paths separately if the edited shader can affect them.
- If compilation/build is needed, use the smallest existing validation path; do not expand this skill into C++ or addon workflow.