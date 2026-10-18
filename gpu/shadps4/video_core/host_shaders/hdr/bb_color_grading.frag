// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's ColorGrading pass
// (ed4c7668), a 16x16x16 color table applied to the image. The table only covers 0..1: above
// the game's white the color is brought down by its largest channel, graded, and scaled back up
// (RenoDX's max-channel LUT scaling), so the HDR part of the 16-bit image survives the grading
// with its hue. The strength of the
// grading (the in-game menu's Scene grading, 0 none .. 1 the game's) comes in bb_hdr.w, set by
// bbport for this draw only. The names are the recompiled shader's: vk_hdr_channel.cpp copies its
// bindings and locations onto these.

#version 450
#extension GL_EXT_fragment_shader_barycentric : require

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
layout(set = 0, binding = 1) uniform texture3D fs_img1;
layout(set = 0, binding = 2) uniform sampler fs_samp0;
layout(set = 0, binding = 3) uniform sampler fs_samp1;

layout(location = 0) pervertexEXT in vec4 fs_in_attr0_p[3];
layout(location = 0) out vec4 frag_color0;

void main() {
    precise float dx1 = fs_in_attr0_p[1].x - fs_in_attr0_p[0].x;
    precise float dx2 = fs_in_attr0_p[2].x - fs_in_attr0_p[0].x;
    precise float dy1 = fs_in_attr0_p[1].y - fs_in_attr0_p[0].y;
    precise float dy2 = fs_in_attr0_p[2].y - fs_in_attr0_p[0].y;
    vec2 uv = vec2(fma(dx2, gl_BaryCoordEXT.z, fma(dx1, gl_BaryCoordEXT.y, fs_in_attr0_p[0].x)),
                   fma(dy2, gl_BaryCoordEXT.z, fma(dy1, gl_BaryCoordEXT.y, fs_in_attr0_p[0].y)));
    vec4 c = textureLod(sampler2D(fs_img0, fs_samp0), uv, 0.0);
    vec3 v = max(c.xyz, 0.0);
    // Above white, as RenoDX's LUT sampling: the color divided by its largest channel (its hue
    // and saturation kept, unlike clipping each channel at 1), graded, then scaled back up. At or
    // below white the table as the game applies it.
    float top = max(max(v.x, v.y), v.z);
    float scale = max(top, 1.0);
    vec3 sdr = v / scale;
    vec3 graded = max(texture(sampler3D(fs_img1, fs_samp1), sdr * 0.9375 + 0.03125).xyz, 0.0);
    // bb_hdr.w: the strength's bits plus one (0: not set by bbport, the game's grading).
    uint bits = push_data.bb_hdr.w;
    float strength = bits == 0u ? 1.0 : clamp(uintBitsToFloat(bits - 1u), 0.0, 1.0);
    graded = mix(sdr, graded, strength);
    vec3 o = graded * scale;
    // The game's channel order: its target holds them swapped.
    frag_color0 = vec4(o.zyx, max(c.w, 0.0));
    // Chain probe (in-game menu, test; bb_hdr.z): 3 magenta where the input is above white,
    // 4 green where this pass draws.
    uint probe = push_data.bb_hdr.z;
    if ((probe == 3u && top > 1.001) || probe == 4u) {
        frag_color0 = probe == 4u ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 1.0, 1.0);
    } else if (probe == 3u && top > 0.99) {
        frag_color0 = vec4(0.0, 1.0, 1.0, 1.0); // cyan: at the game's white, not above
    }
}
