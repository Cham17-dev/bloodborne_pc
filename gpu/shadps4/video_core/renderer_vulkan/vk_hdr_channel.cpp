// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <vector>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/bb_tonemap_hdr_frag.h"
#include "video_core/renderer_vulkan/vk_hdr_channel.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {
float EnvFloat(const char* name, float fallback, float lo, float hi) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return fallback;
    }
    const float f = std::strtof(v, nullptr);
    return f < lo ? lo : f > hi ? hi : f;
}
} // namespace

HdrChannel& HdrChannel::Get() {
    // Never destroyed: at exit the Vulkan device is gone before static objects, and freeing the
    // images then would crash. The process exit frees them.
    static HdrChannel* const channel = new HdrChannel();
    return *channel;
}

bool HdrChannel::Enabled() {
    static const bool enabled =
        EmulatorSettings.GetHdrTonemapMode() == 3 && EmulatorSettings.IsHdrAllowed();
    return enabled;
}

float HdrChannel::PeakNits() {
    static const float peak = EnvFloat("BB_HDR_PEAK_NITS", 1000.0f, 200.0f, 10000.0f);
    return peak;
}

float HdrChannel::Knee() {
    static const float knee = EnvFloat("BB_HDR_KNEE", 0.6f, 0.3f, 0.95f);
    return knee;
}

u32 HdrChannel::Debug() {
    static const u32 debug = u32(EnvFloat("BB_HDR_DEBUG", 0.0f, 0.0f, 2.0f));
    return debug;
}

vk::ShaderModule HdrChannel::FragmentModule(const Instance& instance) {
    std::scoped_lock lock{mutex};
    if (!fragment) {
        std::vector<u32> spv(std::begin(BB_TONEMAP_HDR_FRAG), std::end(BB_TONEMAP_HDR_FRAG));
        const float paper = float(EmulatorSettings.GetHdrPaperWhite());
        const float peak = std::max(PeakNits() / paper, Knee() + 0.05f);
        SetSpirvSpecConstant(spv, 1, std::bit_cast<u32>(peak));
        SetSpirvSpecConstant(spv, 2, std::bit_cast<u32>(Knee()));
        fragment = vk::UniqueShaderModule{CompileSPV(spv, instance.GetDevice()),
                                          instance.GetDevice()};
        LOG_INFO(Render_Vulkan,
                 "HDR channel: peak {} nits, paper white {} nits (x{:.2f}), knee {:.2f}",
                 PeakNits(), paper, peak, Knee());
    }
    return *fragment;
}

HdrChannel::Slot HdrChannel::Acquire(const Instance& instance, Scheduler& scheduler, u32 w,
                                     u32 h) {
    std::scoped_lock lock{mutex};
    if (w != width || h != height) {
        // Rare (resolution change): nothing may still use the old images.
        scheduler.Finish();
        (void)instance.GetDevice().waitIdle();
        latest.store(-1, std::memory_order_release);
        width = w;
        height = h;
        for (auto& slot : images) {
            slot.view.reset();
            slot.image = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
            slot.image.Create(vk::ImageCreateInfo{
                .imageType = vk::ImageType::e2D,
                .format = vk::Format::eR16G16B16A16Sfloat,
                .extent = {w, h, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eColorAttachment |
                         vk::ImageUsageFlagBits::eSampled,
                .initialLayout = vk::ImageLayout::eUndefined,
            });
            auto [result, view] = instance.GetDevice().createImageViewUnique({
                .image = vk::Image(slot.image),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR16G16B16A16Sfloat,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            });
            ASSERT_MSG(result == vk::Result::eSuccess, "HDR channel: image view: {}",
                       vk::to_string(result));
            slot.view = std::move(view);
            slot.layout = vk::ImageLayout::eUndefined;
        }
        LOG_INFO(Render_Vulkan, "HDR channel: {}x{}", w, h);
    }
    const u32 index = next;
    next ^= 1u;
    auto& slot = images[index];
    const Slot out{index, vk::Image(slot.image), *slot.view, slot.layout};
    slot.layout = vk::ImageLayout::eShaderReadOnlyOptimal; // after the recorded write
    return out;
}

void HdrChannel::Published(u32 index) {
    latest.store(s32(index), std::memory_order_release);
}

std::optional<HdrChannel::Latest> HdrChannel::LatestImage() const {
    std::scoped_lock lock{mutex};
    const s32 index = latest.load(std::memory_order_acquire);
    if (index < 0 || !images[index].view) {
        return std::nullopt;
    }
    return Latest{*images[index].view, width, height};
}

} // namespace Vulkan
