// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D texSampler;

layout (push_constant) uniform settings {
    float gamma;
    bool hdr;
    bool srgb_input;
    float hdr_paper_white; // bbport: nits of SDR white in HDR10 output
    bool hdr_gamma22;      // bbport: decode SDR with a 2.2 power instead of the sRGB curve
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

void main() {
    vec4 color_linear = texture(texSampler, uv);
    if (pp.hdr) {
        // The game's frame is SDR: rebuild the SDR signal (with the user's gamma), decode it
        // as an SDR display would, place its white at paper white and encode it in HDR10.
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        vec3 signal = clamp(gamma(clamp(color_linear.rgb, 0.0, 1.0)), 0.0, 1.0);
        vec3 lin = pp.hdr_gamma22 ? pow(signal, vec3(2.2)) : degamma(signal);
        vec3 nits = max(bt709_to_bt2020(lin), 0.0) * pp.hdr_paper_white;
        color = vec4(pq_encode(nits), 1.0);
    } else {
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        color = vec4(gamma(color_linear.rgb), color_linear.a);
    }
}
