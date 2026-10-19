// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, generic pass (BB_HDR_TONEMAP=4, docs/HDR.md step 3c): the HDR side channel for any
// of the 1380 standard variants of Bloodborne's final composite (bb_tonemap_variants.h). The game
// draws its composite unchanged; this shader replays the same draw into bbport's RGBA16F channel
// image. Its interface is the game shader's: when bbport recompiles a composite variant,
// HdrChannel copies that variant's descriptor bindings, input locations and constant-buffer slot
// onto the decorations named bb_* here, and sets the variant's options as specialization
// constants (vk_hdr_channel.cpp). The math is hdr/bb_composite.glsl (checked against the game's
// code, see there).
//
// Output (RGBA16F), as hdr/bb_tonemap_hdr.frag: r, b, a = gains of red, green and blue from the
// game's color to the untonemapped one (post_process.frag grades and tonemaps it live), g =
// luminance of the game's encoded output (to tell the scene from the UI drawn over it). Additive
// variants (no curve: HDR output tonemapped by a later pass) and curves without a knee get
// gains of 1.

#version 450
#extension GL_EXT_fragment_shader_barycentric : require
#extension GL_GOOGLE_include_directive : require

layout(constant_id = 1) const float BB_HDR_PEAK = 4.926;
layout(constant_id = 2) const float BB_HDR_KNEE = 0.6;
layout(constant_id = 10) const int BB_CURVE = 0;
layout(constant_id = 11) const bool BB_LUMINANCE = false;
layout(constant_id = 12) const int BB_PRE = 0;
layout(constant_id = 13) const bool BB_PRE_LUM = false;
layout(constant_id = 14) const bool BB_COS4 = false;
layout(constant_id = 15) const bool BB_VIG888 = false;
layout(constant_id = 16) const bool BB_GAMMA = true;
layout(constant_id = 17) const bool BB_ADDITIVE = false;
layout(constant_id = 18) const uint BB_BUFFER_INDEX = 0u; // index of the constant buffer in buf_offsets
layout(constant_id = 20) const bool BB_GAIN = true;
layout(constant_id = 21) const bool BB_FLOOR_ON = true;
layout(constant_id = 22) const bool BB_SCENE_MAT = false;
layout(constant_id = 23) const bool BB_GLOW_MAT = false;
layout(constant_id = 24) const bool BB_GRADE = true;
// Mode 5 (the 16-bit chain): drawn right after the game's composite into its own (RGBA16F)
// target with additive blending and color writes only: frag_color0 = the HDR output minus the
// game's (both encoded as the game encodes, before its grain), so the target ends up with the
// HDR color plus the game's grain and alpha.
layout(constant_id = 25) const bool BB_OUTPUT_DELTA = false;

// The game shader's constant buffer (decorations copied from the variant).
layout(set = 0, binding = 0, std430) readonly buffer bb_ssbo_block {
    uint data[];
} bb_ssbo;

// The push constants of every shader bbport recompiles.
// The recompiler's push constants (shader_recompiler/resource.h, PushData) at the C++ struct's
// offsets (checked in vk_rasterizer.cpp); bb_hdr is bbport's, set per draw (vk_rasterizer.cpp).
layout(push_constant, std430) uniform AuxData {
    layout(offset = 0) float xoffset;
    layout(offset = 4) float yoffset;
    layout(offset = 8) float xscale;
    layout(offset = 12) float yscale;
    layout(offset = 16) uvec4 buf_offsets0;
    layout(offset = 32) uvec4 buf_offsets1;
    layout(offset = 48) uvec2 buf_offsets2;
    layout(offset = 72) uint motion_param;
    layout(offset = 76) uint scene_size;
    layout(offset = 80) uvec4 bb_hdr;
    // Mode 5, PsychoV30 (scene): x the display's peak (game white units), y exposure,
    // z highlights, w shadows; bb_hdr2b: x contrast, y saturation (bits plus one, 0: not set).
    layout(offset = 96) uvec4 bb_hdr2a;
    layout(offset = 112) uvec4 bb_hdr2b;
} push_data;

layout(set = 0, binding = 1) uniform texture2D bb_img_scene;
layout(set = 0, binding = 2) uniform texture2D bb_img_mult;
layout(set = 0, binding = 3) uniform texture2D bb_img_bloom;
layout(set = 0, binding = 4) uniform sampler bb_samp_scene;
layout(set = 0, binding = 5) uniform sampler bb_samp_mult;
layout(set = 0, binding = 6) uniform sampler bb_samp_bloom;

layout(location = 0) pervertexEXT in vec4 bb_in_scene[3];
layout(location = 1) pervertexEXT in vec4 bb_in_mult[3];
layout(location = 2) pervertexEXT in vec4 bb_in_bloom[3];
layout(location = 3) pervertexEXT in vec4 bb_in_vig[3];

layout(location = 0) out vec4 frag_color0;
// The game's effects as ratios, for the live sliders of the final pass: rgb = the untonemapped
// color without the bloom (glow) over the color (per row: red, green, blue), a = the luminance
// without the vignettes over with (>= 1). 1 where there is nothing to change.
layout(location = 1) out vec4 frag_aux;

uint buf_off;

float bb_c(uint word) {
    return uintBitsToFloat(bb_ssbo.data[word + buf_off]);
}

#include "bb_composite.glsl"
#include "psychov30.glsl"

float bb_bits(uint bits, float fallback) {
    return bits == 0u ? fallback : uintBitsToFloat(bits - 1u);
}

// Mode 5, tone mapper PsychoV30 (scene): PsychoV30 alone on the scene's linear color, in place of
// the game's curve, to the display's peak (the final pass then only encodes). Anchored at
// the game's mid grey, with its slope there as contrast. The menu's exposure, highlights,
// shadows, contrast and saturation are PsychoV30's own parameters.
bool bb_curve_psychov_scene(BbComposite o, vec3 x, out vec3 t) {
    float grey_in, slope;
    if (!bb_knee(o.curve, 0.18, grey_in, slope)) {
        t = bb_curve(o, x);
        return false;
    }
    float peak = max(bb_bits(push_data.bb_hdr2a.x, BB_HDR_PEAK), 1.0);
    float exposure = bb_bits(push_data.bb_hdr2a.y, 1.0);
    float highlights = bb_bits(push_data.bb_hdr2a.z, 1.0);
    float shadows = bb_bits(push_data.bb_hdr2a.w, 1.0);
    float game_contrast = clamp(grey_in * slope / 0.18, 0.3, 3.0);
    float user_contrast = bb_bits(push_data.bb_hdr2b.x, 1.0);
    float contrast = game_contrast * user_contrast;
    float purity = bb_bits(push_data.bb_hdr2b.y, 1.0);
    vec3 game = bb_curve(o, x);
    if (o.curve != 3) {
        x = max(x, BB_FLOOR); // PsychoV30 needs a positive input
    }
    t = rd_psychov30(x, peak, exposure, highlights, shadows, max(contrast, 0.05), purity, 1.0,
                     vec3(grey_in), vec3(0.18), 1.0, 0, 1.0, RD_PSY30_SOURCE_BOUNDARY_BT709);
    t = max(t, 0.0);
    // Below mid grey, the game's own luminance (its curve's toe lifts the shadows, PsychoV30's
    // single power at mid grey does not), PsychoV30's color kept; up to mid grey, PsychoV30 alone.
    // The sliders still apply (the game's value scaled as they move PsychoV30's).
    const vec3 luma = vec3(0.2126390059, 0.7151686788, 0.0721923154);
    float l_game = dot(max(game, 0.0), luma);
    float l_t = dot(t, luma);
    if (l_t > 1e-6 && l_game < 0.18) {
        // The sliders' effect (exposure, shadows, contrast) as a ratio on PsychoV30's own output.
        float grade = 1.0;
        if (exposure != 1.0 || highlights != 1.0 || shadows != 1.0 || purity != 1.0 ||
            user_contrast != 1.0) {
            vec3 base = rd_psychov30(x, peak, 1.0, 1.0, 1.0, game_contrast, 1.0, 1.0,
                                     vec3(grey_in), vec3(0.18), 1.0, 0, 1.0,
                                     RD_PSY30_SOURCE_BOUNDARY_BT709);
            float l_base = dot(max(base, 0.0), luma);
            grade = l_base > 1e-6 ? l_t / l_base : 1.0;
        }
        float match = l_game * grade / l_t;
        t *= mix(match, 1.0, smoothstep(0.05, 0.18, l_game));
    }
    return true;
}

// The game's interpolation, as recompiled: attribute with the barycentrics.
vec2 interp(vec4 a0, vec4 a1, vec4 a2) {
    vec3 b = gl_BaryCoordEXT;
    return vec2(fma(a2.x - a0.x, b.z, fma(a1.x - a0.x, b.y, a0.x)),
                fma(a2.y - a0.y, b.z, fma(a1.y - a0.y, b.y, a0.y)));
}

void main() {
    // buf_offsets holds one byte per buffer binding, as the recompiler reads it:
    // buf_offsets0 = buffers 0..15, buf_offsets1 = 16..31.
    uvec4 offsets = BB_BUFFER_INDEX < 16u ? push_data.buf_offsets0 : push_data.buf_offsets1;
    uint slot = BB_BUFFER_INDEX & 15u;
    buf_off = bitfieldExtract(offsets[slot >> 2u], int((slot & 3u) * 8u), 8) >> 2u;

    BbComposite o = BbComposite(BB_CURVE, BB_LUMINANCE, BB_PRE, BB_PRE_LUM, BB_COS4, BB_VIG888,
                                BB_GAMMA, BB_ADDITIVE, BB_GAIN, BB_FLOOR_ON, BB_SCENE_MAT,
                                BB_GLOW_MAT, BB_GRADE);

    vec4 scene = textureLod(sampler2D(bb_img_scene, bb_samp_scene),
                            interp(bb_in_scene[0], bb_in_scene[1], bb_in_scene[2]), 0.0);
    vec3 mult = texture(sampler2D(bb_img_mult, bb_samp_mult),
                        interp(bb_in_mult[0], bb_in_mult[1], bb_in_mult[2])).rgb;
    vec3 bloom = textureLod(sampler2D(bb_img_bloom, bb_samp_bloom),
                            interp(bb_in_bloom[0], bb_in_bloom[1], bb_in_bloom[2]), 0.0).rgb;
    vec2 vig_uv = (BB_COS4 || BB_VIG888) ? interp(bb_in_vig[0], bb_in_vig[1], bb_in_vig[2])
                                         : vec2(0.0);

    // The game's output, and the same with the HDR curve.
    vec3 ms = bb_scene_tint(mult);
    vec3 x = bb_exposed(o, scene, ms, vig_uv);
    vec3 base = bb_bloom_base(mult);
    vec3 sdr_t = bb_curve(o, x);
    vec3 hdr_t;
    // Mode 5: the knee live (in-game menu, Highlight range; bb_hdr.y set for this draw by
    // bbport, the fraction's bits plus one; 0 = the default).
    float knee = BB_HDR_KNEE;
    if (BB_OUTPUT_DELTA && push_data.bb_hdr.y != 0u) {
        knee = clamp(uintBitsToFloat(push_data.bb_hdr.y - 1u), 0.1, 0.95);
    }
    bool psycho_scene = BB_OUTPUT_DELTA && push_data.bb_hdr2a.x != 0u;
    bool has_hdr = !BB_ADDITIVE && (psycho_scene ? bb_curve_psychov_scene(o, x, hdr_t)
                                               : bb_curve_hdr(o, x, knee, BB_HDR_PEAK, hdr_t));
    vec3 g = bb_glow(o, base, bloom, ms);
    vec3 glow = clamp(g, 0.0, 1.0);
    vec3 sdr_screen = BB_ADDITIVE ? x + g : sdr_t + glow - sdr_t * glow;
    // Screen blend on the SDR part only (above 1 it would subtract).
    vec3 hdr_screen = has_hdr ? hdr_t + glow * (1.0 - min(hdr_t, 1.0)) : sdr_screen;

    // Export order is rows 840, 844, 848; the game's target holds them as BGR (row 848 first,
    // see hdr/bb_tonemap.frag), hence .zyx before the luminance.
    vec3 sdr_graded = bb_grade(o, sdr_screen).zyx;
    vec3 hdr_graded = bb_grade(o, hdr_screen).zyx;
    if (BB_OUTPUT_DELTA) {
        // The game's bloom, live (in-game menu; bb_hdr.w set for this draw by bbport: the
        // strength's bits plus one, 0 = the game's): the target is the composite with its glow
        // scaled, so the difference can also take light away.
        uint bloom_bits = push_data.bb_hdr.w;
        float k = (bloom_bits == 0u || BB_ADDITIVE) ? 1.0
                                                    : clamp(uintBitsToFloat(bloom_bits - 1u), 0.0, 4.0);
        bool scaled = abs(k - 1.0) > 1e-3;
        vec3 target = has_hdr ? hdr_graded : clamp(sdr_graded, 0.0, 1.0);
        if (scaled) {
            vec3 glow_k = clamp(g * k, 0.0, 1.0);
            vec3 sdr_screen_k = sdr_t + glow_k - sdr_t * glow_k;
            vec3 hdr_screen_k = has_hdr ? hdr_t + glow_k * (1.0 - min(hdr_t, 1.0)) : sdr_screen_k;
            target = has_hdr ? bb_grade(o, hdr_screen_k).zyx
                             : clamp(bb_grade(o, sdr_screen_k).zyx, 0.0, 1.0);
        }
        vec3 delta = vec3(0.0);
        if (has_hdr || scaled) {
            if (BB_GAMMA) {
                vec3 hdr_encoded = exp2(bb_c(808u) * log2(max(target, 0.0) + BB_EPS));
                vec3 sdr_game = exp2(bb_c(808u) * log2(clamp(sdr_graded + BB_EPS, 0.0, 1.0)));
                delta = hdr_encoded - sdr_game;
            } else {
                delta = max(target, 0.0) - clamp(sdr_graded, 0.0, 1.0);
            }
        }
        // PsychoV30 (scene) replaces the game's curve, so it may also darken.
        frag_color0 = vec4((scaled || psycho_scene) ? delta : max(delta, 0.0), 0.0);
        // Chain probe (in-game menu, test; bb_hdr.z set for this draw by bbport): green
        // where this draw adds light above the game's white (red and blue pushed far below 0,
        // so the green stays whatever is added later).
        if (push_data.bb_hdr.z == 1u && max(max(delta.x, delta.y), delta.z) > 0.01) {
            frag_color0 = vec4(-64.0, 8.0, -64.0, 0.0);
        }
        // Mode 5 has a single color target; the side-channel ratios below are not used.
        frag_aux = vec4(1.0);
        return;
    }

    // Mode 4 uses a separate gain channel instead of changing the game's target.
    const vec3 luma_bgr = vec3(0.0721923154, 0.7151686788, 0.2126390059);
    float encode = BB_GAMMA ? bb_c(808u) : 1.0 / 2.2;
    vec3 sdr_encoded = exp2(encode * log2(clamp(sdr_graded + BB_EPS, 0.0, 1.0)));
    float sdr_luma = dot(max(sdr_graded, 0.0), luma_bgr);
    float hdr_luma = dot(max(hdr_graded, 0.0), luma_bgr);
    // A gain per channel, from the color matrix's rows (red, green, blue): the untonemapped
    // color over the game's (a saturated light keeps its color above white); where the game's
    // channel is near zero, the luminance gain. The final pass grades and tonemaps (BB_HDR_PEAK
    // is 10000 nits here: the curve is the game's slope continued).
    float gain = (has_hdr && sdr_luma > 1e-4) ? hdr_luma / sdr_luma : 1.0;
    vec3 gains = vec3(clamp(gain, 1.0, 64.0));
    if (has_hdr) {
        vec3 sdr_rows = max(bb_grade(o, sdr_screen), 0.0);
        vec3 hdr_rows = max(bb_grade(o, hdr_screen), 0.0);
        for (int i = 0; i < 3; ++i) {
            gains[i] = sdr_rows[i] > 0.02 ? hdr_rows[i] / sdr_rows[i] : gains[i];
        }
        gains = clamp(gains, 0.0, 64.0);
    }
    // r, b, a: gains of red, green, blue; g: the reference luminance (scene / UI test).
    frag_color0 = vec4(gains.r, dot(sdr_encoded, luma_bgr), gains.g, gains.b);

    // Effect ratios, on the untonemapped color (the final pass scales it before its grading).
    vec4 aux = vec4(1.0);
    if (has_hdr) {
        vec3 rows = max(bb_grade(o, hdr_screen), 0.0);
        vec3 rows_no_bloom = max(bb_grade(o, hdr_t), 0.0);
        for (int i = 0; i < 3; ++i) {
            aux[i] = rows[i] > 1e-4 ? clamp(rows_no_bloom[i] / rows[i], 0.0, 1.0) : 1.0;
        }
        if (BB_COS4 || BB_VIG888) {
            BbComposite no_vig = o;
            no_vig.cos4 = false;
            no_vig.vig888 = false;
            vec3 x_nv = bb_exposed(no_vig, scene, ms, vig_uv);
            vec3 t_nv;
            if (bb_curve_hdr(o, x_nv, knee, BB_HDR_PEAK, t_nv)) {
                vec3 screen_nv = t_nv + glow * (1.0 - min(t_nv, 1.0));
                float l_nv = dot(max(bb_grade(o, screen_nv), 0.0), vec3(0.2126390059, 0.7151686788,
                                                                       0.0721923154));
                float l = dot(rows, vec3(0.2126390059, 0.7151686788, 0.0721923154));
                aux.a = l > 1e-4 ? clamp(l_nv / l, 1.0, 16.0) : 1.0;
            }
        }
    }
    frag_aux = aux;
}
