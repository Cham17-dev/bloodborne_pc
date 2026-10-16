// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's display copy
// (38d65b32), which copies its finished image into the display buffer through its brightness
// curve (a 1D table per channel, gamma-encoded in, linear out: the display buffer is sRGB). The
// table covers 0..1; above the game's white the light continues as the encoding's power (2.2)
// times the table's value at white. The names are the recompiled shader's: vk_hdr_channel.cpp
// copies its bindings and locations onto these.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(push_constant, std430) uniform AuxData {
    float xoffset;
    float yoffset;
    float xscale;
    float yscale;
    uvec4 ud_regs0;
    uvec4 ud_regs1;
    uvec4 ud_regs2;
    uvec4 ud_regs3;
    uvec4 buf_offsets0;
    uvec4 buf_offsets1;
    uvec2 buf_offsets2;
    uint motion_param;
    uint scene_size;
} push_data;

layout(set = 0, binding = 0) uniform texture2D fs_img0;
layout(set = 0, binding = 1) uniform texture1DArray fs_img1;
layout(set = 0, binding = 2) uniform sampler fs_samp0;

layout(location = 0) out vec4 frag_color0;

float brightness(float v, float row) {
    if (v <= 1.0) {
        return texture(sampler1DArray(fs_img1, fs_samp0), vec2(v, row)).x;
    }
    return texture(sampler1DArray(fs_img1, fs_samp0), vec2(1.0, row)).x * pow(v, 2.2);
}

void main() {
    uint w = push_data.scene_size & 65535u;
    uint h = push_data.scene_size >> 16u;
    vec2 scale = vec2(1920.0 / float(w == 0u ? 1920u : w), 1080.0 / float(h == 0u ? 1080u : h));
    vec4 v = texelFetch(fs_img0, ivec2(floor(gl_FragCoord.xy * scale)), 0);
    frag_color0 = vec4(brightness(v.z, 2.0), brightness(v.y, 1.0), brightness(v.x, 0.0), v.w);
}
