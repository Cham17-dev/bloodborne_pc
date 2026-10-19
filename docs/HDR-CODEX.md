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
