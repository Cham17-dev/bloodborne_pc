//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Frame;
}

namespace Vulkan::HostPasses {

class PostProcessingPass {
public:
    struct Settings {
        float gamma = 1.0f;
        u32 hdr = 0;
        u32 srgb_input = 0;
        // bbport HDR: luminance of SDR white in nits, and 2.2 power decode of the SDR frame.
        float hdr_paper_white = 203.0f;
        u32 hdr_gamma22 = 1;
        u32 hdr_test_pattern = 0;
        // bbport HDR side channel (vk_hdr_channel.h): use it, debug view, peak luminance.
        u32 hdr_channel = 0;
        u32 hdr_debug = 0;
        float hdr_peak_nits = 1000.0f;
        // bbport HDR, live (in-game menu): gains exponent, boost of the SDR highlights.
        float hdr_strength = 1.0f;
        float hdr_boost = 0.0f;
        // bbport HDR comparison (in-game menu, F9): 0 HDR, 1 the game's SDR, 2 split (SDR left).
        u32 hdr_compare = 0;
        // bbport HDR, live: HUD nits, and the scene's grading (exposure; contrast, highlights,
        // shadows, saturation, highlight saturation in -1..1, 0 neutral; blowout 0..1).
        float hdr_ui_nits = 203.0f;
        float hdr_exposure = 1.0f;
        float hdr_contrast = 0.0f;
        float hdr_highlights = 0.0f;
        float hdr_shadows = 0.0f;
        float hdr_saturation = 0.0f;
        float hdr_highlight_saturation = 0.0f;
        float hdr_blowout = 0.0f;
        // bbport HDR, live: tonemapper of the final pass (0 Neutwo, 1 PsychoV30) and Neutwo's
        // scaling (0 luminance, 1 per channel).
        u32 hdr_tonemapper = 0;
        u32 hdr_per_channel = 0;
        // bbport HDR, live: the game's bloom and vignettes (1 = as drawn, 0 none, 2 double).
        float hdr_bloom = 1.0f;
        float hdr_vignette = 1.0f;
    };

    void Create(vk::Device device, vk::Format surface_format);

    /// `hdr_channel`: the HDR side channel's image (ShaderReadOnlyOptimal), or null.
    void Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                Frame& output, Settings settings, vk::ImageView hdr_channel = {},
                vk::ImageView hdr_aux = {});

private:
    vk::UniquePipeline pipeline{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniqueDescriptorSetLayout desc_set_layout{};
    vk::UniqueSampler sampler{};
};

} // namespace Vulkan::HostPasses
