// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's display copy
// (38d65b32), which copies its finished image into the display buffer through its brightness
// curve (a 1D table per channel). The table covers 0..1; above the game's white the value goes on
// as the table's value at white times the input (still in the chain's gamma encoding: the final
// pass decodes it, post_process.frag native16_nits). The names are the recompiled shader's: vk_hdr_channel.cpp
// copies its bindings and locations onto these.

#version 450
#extension GL_EXT_samplerless_texture_functions : require

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
} push_data;

layout(set = 0, binding = 0) uniform texture2D fs_img0;
layout(set = 0, binding = 1) uniform texture1DArray fs_img1;
layout(set = 0, binding = 2) uniform sampler fs_samp0;

layout(location = 0) out vec4 frag_color0;

float brightness(float v, float row) {
    if (v <= 1.0) {
        return texture(sampler1DArray(fs_img1, fs_samp0), vec2(v, row)).x;
    }
    return texture(sampler1DArray(fs_img1, fs_samp0), vec2(1.0, row)).x * v;
}

void main() {
    uint w = push_data.scene_size & 65535u;
    uint h = push_data.scene_size >> 16u;
    vec2 scale = vec2(1920.0 / float(w == 0u ? 1920u : w), 1080.0 / float(h == 0u ? 1080u : h));
    vec4 v = texelFetch(fs_img0, ivec2(floor(gl_FragCoord.xy * scale)), 0);
    frag_color0 = vec4(brightness(v.z, 2.0), brightness(v.y, 1.0), brightness(v.x, 0.0), v.w);
    // Chain probe (in-game menu, test; bb_hdr.z): 2 magenta where the finished image is above
    // the game's white.
    if (push_data.bb_hdr.z == 2u && max(max(v.x, v.y), v.z) > 1.001) {
        frag_color0 = vec4(1.0, 0.0, 1.0, v.w);
    }
}
