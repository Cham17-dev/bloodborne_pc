// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later

// bbport: Dear ImGui's fragment shader for an HDR10 swapchain. ImGui's colors are SDR (sRGB
// encoded); they are decoded, placed at a fixed paper white and encoded in Rec.2020 PQ, so the
// settings menu and the FPS counter are not drawn at full PQ brightness.
// The interface matches imgui_impl_vulkan's built-in shader (texture in set 0, sampler in set 1).

#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform texture2D _Texture;
layout(set = 1, binding = 0) uniform sampler _Sampler;
layout(location = 0) in struct {
    vec4 Color;
    vec2 UV;
} In;

const float OVERLAY_NITS = 203.0;

vec3 pq_encode(vec3 nits) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

void main() {
    vec4 c = In.Color * texture(sampler2D(_Texture, _Sampler), In.UV.st);
    vec3 lin = pow(clamp(c.rgb, 0.0, 1.0), vec3(2.2));
    vec3 bt2020 = vec3(dot(vec3(0.6274040, 0.3292820, 0.0433136), lin),
                       dot(vec3(0.0690970, 0.9195400, 0.0113612), lin),
                       dot(vec3(0.0163916, 0.0880132, 0.8955950), lin));
    fColor = vec4(pq_encode(max(bt2020, 0.0) * OVERLAY_NITS), c.a);
}
