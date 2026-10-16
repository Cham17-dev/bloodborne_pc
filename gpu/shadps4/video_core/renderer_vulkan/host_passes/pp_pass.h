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
    };

    void Create(vk::Device device, vk::Format surface_format);

    /// `hdr_channel`: the HDR side channel's image (ShaderReadOnlyOptimal), or null.
    void Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                Frame& output, Settings settings, vk::ImageView hdr_channel = {});

private:
    vk::UniquePipeline pipeline{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniqueDescriptorSetLayout desc_set_layout{};
    vk::UniqueSampler sampler{};
};

} // namespace Vulkan::HostPasses
