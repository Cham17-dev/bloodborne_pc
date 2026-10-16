// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR, mode 5 (the 16-bit chain, docs/HDR.md): replaces the game's EmissiveToGlowPS
// (57f3b8c2), a copy of the finished image into the glow's source. The copy is clamped to the
// game's white, so the glow (added back over the image after the tone mapping) stays as the
// game draws it while the image itself carries HDR light. The names are the recompiled
// shader's: vk_hdr_channel.cpp copies its bindings and locations onto these.

#version 450
#extension GL_EXT_fragment_shader_barycentric : require

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
    // The game's channel order: its target holds them swapped.
    frag_color0 = vec4(min(c.zyx, vec3(1.0)), c.w);
}
