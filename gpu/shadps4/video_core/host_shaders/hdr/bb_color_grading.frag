// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's ColorGrading pass
// (ed4c7668), a 16x16x16 color table applied to the image. The table only covers 0..1: above
// the game's white each channel keeps the table's color at white, scaled by how far the input
// goes above it, so the HDR part of the 16-bit image survives the grading. The strength of the
// grading (the in-game menu's Scene grading, 0 none .. 1 the game's) comes in ud_regs3.w, set by
// bbport for this draw only. The names are the recompiled shader's: vk_hdr_channel.cpp copies its
// bindings and locations onto these.

#version 450
#extension GL_EXT_fragment_shader_barycentric : require

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
    vec3 sdr = min(v, 1.0);
    vec3 graded = max(texture(sampler3D(fs_img1, fs_samp1), sdr * 0.9375 + 0.03125).xyz, 0.0);
    // ud_regs3.w: the strength's bits plus one (0: not set by bbport, the game's grading).
    uint bits = push_data.ud_regs3.w;
    float strength = bits == 0u ? 1.0 : clamp(uintBitsToFloat(bits - 1u), 0.0, 1.0);
    graded = mix(sdr, graded, strength);
    vec3 above = mix(vec3(1.0), v / max(sdr, vec3(1e-6)), greaterThan(v, vec3(1.0)));
    vec3 o = graded * above;
    // The game's channel order: its target holds them swapped.
    frag_color0 = vec4(o.zyx, max(c.w, 0.0));
}
