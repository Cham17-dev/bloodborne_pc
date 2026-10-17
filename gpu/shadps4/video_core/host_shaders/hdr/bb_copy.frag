// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's PointSampleColor1
// (f7229091), a plain copy with the channel swap, for the chain probe of the in-game menu: with
// probe 5 (ud_regs3.z, set by bbport for this draw), magenta where the copied image is above the
// game's white. Otherwise the same copy. The names are the recompiled shader's:
// vk_hdr_channel.cpp copies its bindings and locations onto these.

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
layout(set = 0, binding = 1) uniform sampler fs_samp0;

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
    frag_color0 = vec4(c.zyx, c.w);
    if (push_data.ud_regs3.z == 5u && max(max(c.x, c.y), c.z) > 1.001) {
        frag_color0 = vec4(1.0, 0.0, 1.0, c.w);
    }
}
