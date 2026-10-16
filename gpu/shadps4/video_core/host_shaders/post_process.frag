// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D texSampler;
// bbport HDR side channel (vk_hdr_channel.h): r, b, a = gains of the red, green and blue
// channels from the game's color to the untonemapped one (graded and tonemapped here, live),
// g = the game's output luminance (gamma 2.2 encoded) where they were computed.
layout (binding = 1) uniform sampler2D hdrChannel;
// bbport: the channel's effect ratios: rgb = untonemapped color without the game's bloom over
// with (per channel), a = without its vignettes over with.
layout (binding = 2) uniform sampler2D hdrAux;

layout (push_constant) uniform settings {
    float gamma;
    bool hdr;
    bool srgb_input;
    float hdr_paper_white; // bbport: nits of SDR white in HDR10 output
    bool hdr_gamma22;      // bbport: decode SDR with a 2.2 power instead of the sRGB curve
    bool hdr_test_pattern; // bbport: HDR10 test pattern instead of the game
    bool hdr_channel;      // bbport: apply the HDR side channel's gains
    uint hdr_debug;        // bbport: 1 = gain heat map, 2 = pixels taken as UI in red
    float hdr_peak_nits;   // bbport: peak luminance
    float hdr_strength;    // bbport: exponent of the channel's gains (1 = as computed)
    float hdr_boost;       // bbport: boost of the scene's SDR highlights (0 .. 1: white to peak)
    uint hdr_compare;      // bbport: 0 HDR, 1 the game's SDR, 2 split (SDR left, HDR right)
    float hdr_ui_nits;     // bbport: nits of the HUD's white (pixels taken as UI)
    // bbport: the scene's grading, live (in-game menu, like RenoDX's): exposure (1 neutral),
    // contrast, highlights, shadows, saturation, highlight saturation (-1 .. 1, 0 neutral),
    // blowout (0 .. 1).
    float hdr_exposure;
    float hdr_contrast;
    float hdr_highlights;
    float hdr_shadows;
    float hdr_saturation;
    float hdr_highlight_saturation;
    float hdr_blowout;
    uint hdr_tonemapper;   // bbport: 0 Neutwo, 1 PsychoV30 (RenoDX)
    uint hdr_per_channel;  // bbport: Neutwo per channel (else on luminance, hue kept)
    float hdr_bloom;       // bbport: the game's bloom, 0 none .. 1 the game's .. 2 double
    float hdr_vignette;    // bbport: the game's vignettes, 0 none .. 1 the game's .. 2 double
    bool hdr_native16;     // bbport: the frame is the 16-bit chain's (linear, above 1 = HDR)
} pp;

const vec3 LUMA709 = vec3(0.2126390059, 0.7151686788, 0.0721923154);

#include "hdr/psychov30.glsl"

// Neutwo shoulder: x below k unchanged, above it rising toward `peak` (never reaching it).
float neutwo_shoulder(float x, float k, float peak) {
    if (x <= k) {
        return x;
    }
    float over = x - k, room = max(peak - k, 1e-3);
    return k + over * inversesqrt(1.0 + (over / room) * (over / room));
}

// The untonemapped, graded scene (1 = paper white) to the display's range, like RenoDX's tone
// mapper: Neutwo from 0.6 of the peak (on luminance, or per channel), or PsychoV30.
vec3 tonemap_scene(vec3 v, float peak) {
    if (pp.hdr_tonemapper == 1u) {
        return max(rd_psychov30(v, peak), 0.0);
    }
    float k = 0.6 * peak;
    if (pp.hdr_per_channel != 0u) {
        return vec3(neutwo_shoulder(v.r, k, peak), neutwo_shoulder(v.g, k, peak),
                    neutwo_shoulder(v.b, k, peak));
    }
    float l = dot(v, LUMA709);
    v *= l > 0.0 ? neutwo_shoulder(l, k, peak) / l : 1.0;
    // A saturated color can still have a channel above the peak: scaled down, hue kept.
    float m = max(max(v.r, v.g), v.b);
    return m > peak ? v * (peak / m) : v;
}


// The scene's grading (linear BT.709, 1 = paper white), around mid grey 0.18: exposure, then
// contrast (a power around mid grey, 0.5 .. 2), highlights (above mid grey) and shadows (below,
// raised when positive), all on luminance with the color scaled; then saturation, more or less in
// the highlights, and blowout (the brightest colors desaturated toward white near the peak).
vec3 grade_scene(vec3 v, float peak) {
    const float grey = 0.18;
    v *= pp.hdr_exposure;
    float l = dot(v, LUMA709);
    if (l > 1e-6) {
        float n = grey * pow(l / grey, exp2(pp.hdr_contrast));
        if (n > grey) {
            n *= pow(n / grey, 0.4 * pp.hdr_highlights);
        } else {
            n *= min(pow(n / grey, -0.25 * pp.hdr_shadows), 4.0);
        }
        v *= n / l;
        l = n;
    }
    float sat = (1.0 + pp.hdr_saturation) *
                mix(1.0, 1.0 + pp.hdr_highlight_saturation, smoothstep(grey, 1.5, l));
    v = max(l + (v - l) * sat, 0.0);
    return mix(v, vec3(l), pp.hdr_blowout * smoothstep(0.25 * peak, peak, l));
}

const float cutoff = 0.0031308, a = 1.055, b = 0.055, d = 12.92;
vec3 gamma(vec3 rgb) {
    return mix(
        a * pow(rgb, vec3(1.0 / (2.4 + 1.0 - pp.gamma))) - b,
        d * rgb / pp.gamma,
        lessThan(rgb, vec3(cutoff))
    );
}

// Exact inverse of gamma() at unit gamma, for buffers that are sRGB encoded but must be sampled
// through a UNORM view because Vulkan has no sRGB variant of the 10-bit format.
vec3 degamma(vec3 rgb) {
    return mix(
        pow(max(rgb + b, 0.0) / a, vec3(2.4)),
        rgb / d,
        lessThan(rgb, vec3(d * cutoff))
    );
}

// bbport HDR10: Rec.709 -> Rec.2020 primaries, then SMPTE ST 2084 (PQ) encoding.
vec3 bt709_to_bt2020(vec3 c) {
    return vec3(dot(vec3(0.6274040, 0.3292820, 0.0433136), c),
                dot(vec3(0.0690970, 0.9195400, 0.0113612), c),
                dot(vec3(0.0163916, 0.0880132, 0.8955950), c));
}

vec3 pq_encode(vec3 nits) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

// An SDR signal (sRGB-encoded, 0..1) as an SDR display shows it, in Rec.2020 nits, each Rec.709
// channel first multiplied by `gains` (the HDR side channel's; 1 for plain SDR).
vec3 sdr_to_nits(vec3 signal, vec3 gains) {
    vec3 lin = pp.hdr_gamma22 ? pow(signal, vec3(2.2)) : degamma(signal);
    return max(bt709_to_bt2020(lin * gains), 0.0) * pp.hdr_paper_white;
}

// bbport HDR test pattern, four rows from the top:
//  1. SDR grey ramp, 32 steps from black to white, through the same path as the game;
//  2. grey patches at 0.1, 1, 10, 50, 100 nits, paper white, 300, 400, 600, 800 nits;
//  3. grey patches at 1000, 1200, 1500, 2000, 2500, 3000, 4000, 6000, 8000, 10000 nits
//     (patches that look the same are above what the display can show);
//  4. SDR color bars: white, yellow, cyan, green, magenta, red, blue, black.
vec3 test_pattern_nits(vec2 p) {
    int row = int(min(floor(p.y * 4.0), 3.0));
    float fy = fract(p.y * 4.0);
    if (fy < 0.03 || fy > 0.97) {
        return vec3(0.0);
    }
    if (row == 0) {
        return sdr_to_nits(vec3(min(floor(p.x * 32.0), 31.0) / 31.0), vec3(1.0));
    }
    if (row == 3) {
        const vec3 bars[8] = vec3[](vec3(1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0),
                                    vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                                    vec3(0.0, 0.0, 1.0), vec3(0.0));
        return sdr_to_nits(bars[int(min(floor(p.x * 8.0), 7.0))], vec3(1.0));
    }
    const float low[10] = float[](0.1, 1.0, 10.0, 50.0, 100.0, -1.0, 300.0, 400.0, 600.0, 800.0);
    const float high[10] = float[](1000.0, 1200.0, 1500.0, 2000.0, 2500.0, 3000.0, 4000.0,
                                   6000.0, 8000.0, 10000.0);
    int i = int(min(floor(p.x * 10.0), 9.0));
    float fx = fract(p.x * 10.0);
    if (fx < 0.04 || fx > 0.96) {
        return vec3(0.0);
    }
    float nits = row == 1 ? (low[i] < 0.0 ? pp.hdr_paper_white : low[i]) : high[i];
    return vec3(nits);
}

// bbport HDR, 16-bit chain (BB_HDR_TONEMAP=5): the game drew its frame in RGBA16F, linear, its
// white at 1 and its highlights above. The SDR part is decoded like the game's SDR (sRGB or 2.2);
// above 1 the values are the game's own, then shaped live like the side channel's.
vec3 native16_nits(vec3 v) {
    v = max(v, 0.0);
    vec3 sdr = min(v, 1.0);
    vec3 signal = clamp(gamma(sdr), 0.0, 1.0);
    bool sdr_side = pp.hdr_compare == 1u || (pp.hdr_compare == 2u && uv.x < 0.5);
    if (sdr_side) {
        return sdr_to_nits(signal, vec3(1.0));
    }
    vec3 lin_sdr = pp.hdr_gamma22 ? pow(signal, vec3(2.2)) : degamma(signal);
    // Above white, each channel keeps its ratio to its SDR value.
    vec3 lin = lin_sdr * mix(vec3(1.0), v / max(sdr, 1e-6), greaterThan(v, vec3(1.0)));
    float peak = max(pp.hdr_peak_nits / pp.hdr_paper_white, 1.0);
    float l = dot(lin, LUMA709), l_sdr = dot(lin_sdr, LUMA709);
    // Strength: the luminance above white raised to this power.
    if (l > 1.0) {
        lin *= pow(l, max(pp.hdr_strength, 0.0) - 1.0);
    }
    float y = clamp((l_sdr - 0.45) / 0.55, 0.0, 1.0);
    lin *= 1.0 + clamp(pp.hdr_boost, 0.0, 1.0) * (peak - 1.0) * y * y * y;
    vec3 t = tonemap_scene(grade_scene(max(lin, 0.0), peak), peak);
    vec3 nits = min(max(bt709_to_bt2020(t), 0.0) * pp.hdr_paper_white, vec3(pp.hdr_peak_nits));
    if (pp.hdr_debug == 1u) {
        float gain = dot(t, LUMA709) / max(l_sdr, 1e-4);
        float k = clamp((gain - 1.0) / (max(peak, 1.001) - 1.0), 0.0, 1.0);
        nits = gain > 1.001 ? mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), k) * pp.hdr_paper_white
                            : vec3(dot(nits, vec3(0.2627, 0.6780, 0.0593)) * 0.5);
    }
    return nits;
}

void main() {
    vec4 color_linear = texture(texSampler, uv);
    if (pp.hdr && pp.hdr_test_pattern) {
        color = vec4(pq_encode(test_pattern_nits(uv)), 1.0);
    } else if (pp.hdr && pp.hdr_native16) {
        vec3 nits = native16_nits(color_linear.rgb);
        if (pp.hdr_compare == 2u && abs(uv.x - 0.5) * float(textureSize(texSampler, 0).x) < 1.5) {
            nits = vec3(pp.hdr_paper_white); // split line
        }
        color = vec4(pq_encode(nits), 1.0);
    } else if (pp.hdr) {
        // The game's frame is SDR: rebuild the SDR signal (with the user's gamma), decode it
        // as an SDR display would, place its white at paper white and encode it in HDR10.
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        vec3 signal = clamp(gamma(clamp(color_linear.rgb, 0.0, 1.0)), 0.0, 1.0);
        vec3 nits = sdr_to_nits(signal, vec3(1.0));
        // Comparison: the game's SDR (at paper white) on the whole frame or on its left half.
        bool sdr_side = pp.hdr_compare == 1u || (pp.hdr_compare == 2u && uv.x < 0.5);
        if (pp.hdr_channel && !sdr_side) {
            // Where the finished frame is still what the tonemap wrote (same luminance, give or
            // take the game's AA, grain and brightness setting), it is the scene: its highlights
            // are raised by the channel's gain. Elsewhere (UI drawn over it) it stays SDR.
            vec4 channel = texture(hdrChannel, uv);
            float luma = dot(signal, vec3(0.2126390059, 0.7151686788, 0.0721923154));
            // The game's AA (after the tonemap) blends each pixel with its neighbours: on thin
            // contrasted details (window bars against bright panes) the frame then lies between
            // the tonemap's values around it. So the frame is compared with the range of the
            // channel over the 3x3 texels around it, not with the centre texel alone.
            ivec2 size = textureSize(hdrChannel, 0);
            ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
            float g_min = channel.g, g_max = channel.g;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    float g = texelFetch(hdrChannel, clamp(texel + ivec2(dx, dy), ivec2(0),
                                                           size - 1), 0).g;
                    g_min = min(g_min, g);
                    g_max = max(g_max, g);
                }
            }
            float miss = max(max(g_min - luma, luma - g_max), 0.0);
            // The tolerance widens with brightness: the game's brightness curve (applied by its
            // display copy, after the tonemap) moves the brightest values the most, while UI over
            // the scene (light text on a dark scene, dark frames over a bright one) differs by
            // far more than that.
            float tolerance = 0.04 + 0.22 * g_max * g_max;
            float scene = 1.0 - smoothstep(tolerance, tolerance + 0.10, miss);
            // Live shaping (in-game menu), in units of paper white:
            //  - strength: the channel's gains raised to this power (more or less of the scene's
            //    highlights above the game's white);
            //  - boost: the scene's SDR highlights (above about half of white) raised toward the
            //    peak, the game's white reaching 1 + boost (peak - 1);
            //  - then a shoulder from 0.6 of the peak, so nothing clips.
            const vec3 luma709 = vec3(0.2126390059, 0.7151686788, 0.0721923154);
            float peak = max(pp.hdr_peak_nits / pp.hdr_paper_white, 1.0);
            vec3 gains = pow(max(vec3(channel.r, channel.b, channel.a), 0.0),
                             vec3(max(pp.hdr_strength, 0.0)));
            gains = mix(vec3(1.0), gains, scene);
            vec3 lin = pp.hdr_gamma22 ? pow(signal, vec3(2.2)) : degamma(signal);
            float l_sdr = dot(lin, luma709);
            float y = clamp((l_sdr - 0.45) / 0.55, 0.0, 1.0);
            float boost = 1.0 + scene * clamp(pp.hdr_boost, 0.0, 1.0) * (peak - 1.0) * y * y * y;
            // The game's bloom and vignettes, more or less (1 = as drawn).
            vec4 aux = clamp(texture(hdrAux, uv), vec4(0.0), vec4(1.0, 1.0, 1.0, 16.0));
            vec3 effects = mix(aux.rgb, vec3(1.0), max(pp.hdr_bloom, 0.0)) *
                           mix(aux.a, 1.0, max(pp.hdr_vignette, 0.0));
            effects = mix(vec3(1.0), max(effects, 0.0), scene);
            vec3 v = tonemap_scene(grade_scene(max(lin * gains * boost * effects, 0.0), peak),
                                   peak);
            nits = min(max(bt709_to_bt2020(v), 0.0) * pp.hdr_paper_white, vec3(pp.hdr_peak_nits));
            // The HUD (pixels taken as UI): the game's SDR at its own brightness.
            vec3 ui = sdr_to_nits(signal, vec3(1.0)) * (pp.hdr_ui_nits / pp.hdr_paper_white);
            nits = mix(ui, nits, scene);
            float gain = dot(v, luma709) / max(l_sdr, 1e-4);
            if (pp.hdr_debug == 1u) {
                // Gain heat map: grey = 1 (SDR), blue to red = up to the peak.
                float peak_gain = max(pp.hdr_peak_nits / pp.hdr_paper_white, 1.001);
                float t = clamp((gain - 1.0) / (peak_gain - 1.0), 0.0, 1.0);
                nits = gain > 1.001 ? mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), t) *
                                          pp.hdr_paper_white
                                    : vec3(dot(nits, vec3(0.2627, 0.6780, 0.0593)) * 0.5);
            } else if (pp.hdr_debug == 2u && scene < 0.5) {
                nits = mix(nits, vec3(pp.hdr_paper_white, 0.0, 0.0), 0.6); // taken as UI
            }
        }
        if (pp.hdr_compare == 2u && abs(uv.x - 0.5) * float(textureSize(texSampler, 0).x) < 1.5) {
            nits = vec3(pp.hdr_paper_white); // split line
        }
        color = vec4(pq_encode(nits), 1.0);
    } else {
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        color = vec4(gamma(color_linear.rgb), color_linear.a);
    }
}
