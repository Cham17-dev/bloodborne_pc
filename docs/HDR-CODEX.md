# Mode 5 + PsychoV30: linear-light temporal reconstruction

Experimental branch `hdr-codex-psychov30`, based on `hdr` at
`d3aaa281dbda5258a5629c40415712a14c991ade`. This branch does not change the PsychoV30
curve, its shadow protection, the LUT, the default artistic settings, or the no-upscaler path.

## First change

Mode 5's completed post-composite frame carries gamma-encoded values (including values above
white) in RGBA16F. `RunScaled` previously supplied those values directly to reconstruction
configured for HDR input. Floating-point storage does not make an encoded signal linear.

The new path is:

1. Read the active render rectangle from the RGBA16F scene proxy.
2. Decode the chain's gamma 2.2 encoding into a separate RGBA16F input.
3. Run DLSS, FSR 3/4 or TAA on linear light. Sharpening operates on the linear result.
4. Encode that result back to gamma 2.2 in the output-size UI image.
5. Snapshot the encoded image, then let the game's UI and display passes continue.

The existing final HDR output pass still controls Black Floor, paper white, peak, and PQ.
These conversions do not introduce a second tone mapper. Above-white values are preserved;
only values outside the representable FP16 linear range are capped. Alpha is copied by the
transfer shader. The depth image keeps its original dimensions even when the decoded color
image removes alignment padding.

FSR 3's mode 5 output now uses `output_image` before encoding into `ui_image`. This also ensures
that extra sharpening reads and writes the same image: the floating-point sharpening pipeline
uses `output_view`, which previously differed from the passed UI image in this path.

Temporal history is reset on changes of effective mode 5 tone mapper or full-screen F9 SDR
reference. Continuous grading-slider changes are not covered by this reset yet.

## Verification

- Compile the registered host shaders and HDR library checks with the normal build/CI options.
- Compile and validate `hdr_upscale_transfer.comp` as Vulkan 1.3 SPIR-V.
- `python3 -I docs/hdr-tools/upscale_transfer_check/run.py` executes the production transfer
  functions through glslang, SPIRV-Cross and its CPU backend. It checks 1,541 inputs: grey and
  saturated ramps, black, white, negatives, extreme inputs, finite results, linear-light
  averaging and round trips including both FP16 storage boundaries. Output starts as NaN, so
  an empty dispatch fails. See the script for dependencies; the shader workflow runs it too.
- The numerical test isolates the math. It does not execute the engine's Vulkan integration,
  vendor reconstruction models, or game rendering. The Windows build checks the host code.

## In-game acceptance before merging

Use separate installations for this branch and Claude's reference build. Keep mode 5,
PsychoV30, paper white, peak, game brightness, grading and output resolution identical.

1. Without an upscaler, compare a static dark interior, an exterior, a fire and a blue light.
   This path is outside the change and is the control.
2. Test DLSS/FSR Native AA first with sharpening off, then Quality with the same camera.
   Include TAA at native resolution if used, and test each FSR 4 provider separately.
3. Repeat with moderate sharpening; inspect near-black texture detail, halos and saturated
   lights. Move the camera to check stability. Let history settle before screenshots.
4. Toggle F9 and Vanilla/PsychoV30; inspect the first frames and steady-state HUD brightness.
5. Capture JXR pairs and analyze reference-defined luminance bands. Use repeated captures to
   distinguish animation/grain from rendering changes. In the shadows, report absolute nits
   as well as ratios. This change does not assume that the previously reported 5-18% difference
   was entirely caused by transfer-function handling.

Cost: one render-size FP16 image plus two compute passes on the temporal mode 5 path. Runtime
cost and visual quality require measurement on the target GPU. The FSR 1 presentation path,
UI detection, scene-specific variants, and post-LUT peak handling remain separate work.

## HDR code cleanup

The cleanup after `594aeb6` keeps all supported modes and existing settings compatible:

- Remove the unused composite PsychoV30 selector (specialization 19, always set to zero by
  `BuildComposite`) and its obsolete `bb_curve_psychov` function. This is separate from the
  live PsychoV30 scene function and the supported hybrid mode; both remain available.
- Return from the mode 5 delta path before computing the legacy mode 4 gain and effect-ratio
  outputs. Mode 5 only writes RGB into the existing game target; it does not consume the
  auxiliary output. No performance gain is claimed: the driver may already eliminate it.
- Share SDR decoding, reference selection, HDR peak conversion, HUD brightness conversion
  and debug display math in `post_process.frag`. Name the tone mapper IDs in GLSL.
- Correct stale header comments which described only the old side-channel implementation.

Validation: 32 host shader configurations compiled and passed SPIR-V validation. A one-off
before/after CPU comparison of the compiled post-process math produced identical results on
65,536 synthetic pixels across native/legacy/SDR paths, tone mappers, grading, compare and
UI/debug settings. Texture fetches were replaced by deterministic constant samples; this
checks the refactor, not real texture filtering or the Vulkan rendering pipeline. The scene
PsychoV30/shadow function and mode 5 delta equations are textually unchanged. The 1,541-case
transfer regression still passes. Windows CI and in-game acceptance remain required.

F9, Vanilla, shader-variant coverage, capture tools and diagnostics are retained for validation;
they are not dead code. This cleanup does not remove the other HDR modes or promise fewer
shaders at runtime, a different picture, or a higher frame rate.
