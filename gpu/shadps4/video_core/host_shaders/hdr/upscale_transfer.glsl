// SPDX-License-Identifier: GPL-2.0-or-later
// Mode 5's post-composite frame is gamma 2.2 encoded, including values above SDR white.
// Temporal reconstruction works in linear light; the game's later UI/display passes still
// expect its original encoding. Neither conversion is a tone mapper or a gamut transform.
#ifndef BBPORT_HDR_UPSCALE_TRANSFER_GLSL
#define BBPORT_HDR_UPSCALE_TRANSFER_GLSL

vec3 bb_upscale_decode(vec3 encoded) {
    // The destination is FP16. Keep extreme inputs representable instead of writing infinity
    // into the temporal history. Normal mode 5 output (peak / paper white) is far below this.
    return min(pow(max(encoded, vec3(0.0)), vec3(2.2)), vec3(65504.0));
}

vec3 bb_upscale_encode(vec3 linear) {
    return pow(clamp(linear, vec3(0.0), vec3(65504.0)), vec3(1.0 / 2.2));
}

#endif
