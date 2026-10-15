// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D texSampler;
// bbport HDR side channel (vk_hdr_channel.h): r = HDR gain, g = the game's output luminance
// (gamma 2.2 encoded) where the gain was computed, b = the game's linear luminance.
layout (binding = 1) uniform sampler2D hdrChannel;

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
} pp;

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

// An SDR signal (sRGB-encoded, 0..1) as an SDR display shows it, in Rec.2020 nits.
vec3 sdr_to_nits(vec3 signal) {
    vec3 lin = pp.hdr_gamma22 ? pow(signal, vec3(2.2)) : degamma(signal);
    return max(bt709_to_bt2020(lin), 0.0) * pp.hdr_paper_white;
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
        return sdr_to_nits(vec3(min(floor(p.x * 32.0), 31.0) / 31.0));
    }
    if (row == 3) {
        const vec3 bars[8] = vec3[](vec3(1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0),
                                    vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                                    vec3(0.0, 0.0, 1.0), vec3(0.0));
        return sdr_to_nits(bars[int(min(floor(p.x * 8.0), 7.0))]);
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

void main() {
    vec4 color_linear = texture(texSampler, uv);
    if (pp.hdr && pp.hdr_test_pattern) {
        color = vec4(pq_encode(test_pattern_nits(uv)), 1.0);
    } else if (pp.hdr) {
        // The game's frame is SDR: rebuild the SDR signal (with the user's gamma), decode it
        // as an SDR display would, place its white at paper white and encode it in HDR10.
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        vec3 signal = clamp(gamma(clamp(color_linear.rgb, 0.0, 1.0)), 0.0, 1.0);
        vec3 nits = sdr_to_nits(signal);
        if (pp.hdr_channel) {
            // Where the finished frame is still what the tonemap wrote (same luminance, give or
            // take the game's AA, grain and brightness setting), it is the scene: its highlights
            // are raised by the channel's gain. Elsewhere (UI drawn over it) it stays SDR.
            vec4 channel = texture(hdrChannel, uv);
            float luma = dot(signal, vec3(0.2126390059, 0.7151686788, 0.0721923154));
            float scene = 1.0 - smoothstep(0.03, 0.10, abs(luma - channel.g));
            float gain = mix(1.0, max(channel.r, 1.0), scene);
            nits = min(nits * gain, vec3(pp.hdr_peak_nits));
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
        color = vec4(pq_encode(nits), 1.0);
    } else {
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        color = vec4(gamma(color_linear.rgb), color_linear.a);
    }
}
