// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/bb_composite_hdr_frag.h"
#include "video_core/host_shaders/bb_tonemap_hdr_frag.h"
#include "video_core/renderer_vulkan/bb_tonemap_variants.h"
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

const TonemapVariant* FindVariant(u64 hash) {
    if (hash == 0 || hash > 0xffffffffULL) {
        return nullptr;
    }
    const auto it = std::lower_bound(
        TonemapVariants.begin(), TonemapVariants.end(), u32(hash),
        [](const TonemapVariant& v, u32 h) { return v.hash < h; });
    return it != TonemapVariants.end() && it->hash == u32(hash) ? &*it : nullptr;
}

// SPIR-V reading and patching for the generic pass: the names (OpName) and the Binding and
// Location decorations of a module.
constexpr u32 SpvOpName = 5, SpvOpDecorate = 71, SpvOpSpecConstantTrue = 48,
              SpvOpSpecConstantFalse = 49, SpvDecorationSpecId = 1, SpvDecorationLocation = 30,
              SpvDecorationBinding = 33;

struct SpvInterface {
    std::unordered_map<std::string, std::vector<u32>> ids; ///< name -> ids
    std::unordered_map<u32, u32> binding;                    ///< id -> Binding
    std::unordered_map<u32, u32> location;                   ///< id -> Location

    std::optional<u32> Binding(const std::string& name) const {
        return Find(name, binding);
    }
    std::optional<u32> Location(const std::string& name) const {
        return Find(name, location);
    }

private:
    std::optional<u32> Find(const std::string& name,
                            const std::unordered_map<u32, u32>& decorations) const {
        const auto it = ids.find(name);
        if (it == ids.end()) {
            return std::nullopt;
        }
        for (const u32 id : it->second) {
            if (const auto d = decorations.find(id); d != decorations.end()) {
                return d->second;
            }
        }
        return std::nullopt;
    }
};

SpvInterface ReadInterface(std::span<const u32> spv) {
    SpvInterface out;
    for (size_t i = 5; i < spv.size();) {
        const u32 count = spv[i] >> 16, opcode = spv[i] & 0xffff;
        if (count == 0 || i + count > spv.size()) {
            break;
        }
        if (opcode == SpvOpName && count >= 3) {
            const char* text = reinterpret_cast<const char*>(&spv[i + 2]);
            out.ids[std::string(text, strnlen(text, (count - 2) * 4))].push_back(spv[i + 1]);
        } else if (opcode == SpvOpDecorate && count >= 4) {
            if (spv[i + 2] == SpvDecorationBinding) {
                out.binding[spv[i + 1]] = spv[i + 3];
            } else if (spv[i + 2] == SpvDecorationLocation) {
                out.location[spv[i + 1]] = spv[i + 3];
            }
        }
        i += count;
    }
    return out;
}

/// Sets the `decoration` literal of every variable called `name`. False if there is none.
bool SetDecoration(std::vector<u32>& spv, const SpvInterface& names, const std::string& name,
                   u32 decoration, u32 value) {
    const auto it = names.ids.find(name);
    if (it == names.ids.end()) {
        return false;
    }
    bool done = false;
    for (size_t i = 5; i < spv.size();) {
        const u32 count = spv[i] >> 16, opcode = spv[i] & 0xffff;
        if (count == 0) {
            break;
        }
        if (opcode == SpvOpDecorate && count >= 4 && spv[i + 2] == decoration &&
            std::ranges::find(it->second, spv[i + 1]) != it->second.end()) {
            spv[i + 3] = value;
            done = true;
        }
        i += count;
    }
    return done;
}

/// Sets a boolean specialization constant (OpSpecConstantTrue / False).
void SetSpecBool(std::vector<u32>& spv, u32 spec_id, bool value) {
    u32 target = 0;
    for (size_t i = 5; i < spv.size();) {
        const u32 count = spv[i] >> 16, opcode = spv[i] & 0xffff;
        if (count == 0) {
            return;
        }
        if (opcode == SpvOpDecorate && count >= 4 && spv[i + 2] == SpvDecorationSpecId &&
            spv[i + 3] == spec_id) {
            target = spv[i + 1];
        } else if ((opcode == SpvOpSpecConstantTrue || opcode == SpvOpSpecConstantFalse) &&
                   count == 3 && target && spv[i + 2] == target) {
            spv[i] = (count << 16) | (value ? SpvOpSpecConstantTrue : SpvOpSpecConstantFalse);
            return;
        }
        i += count;
    }
}

// Generic pass: the HDR shader built for each composite variant (by pgm_hash). Never destroyed,
// like the channel (the device is gone before static objects at exit).
struct CompositeShader {
    std::vector<u32> spv;
    vk::ShaderModule module{};
    bool conflicting = false; ///< two permutations with different interfaces: not used
};
std::mutex composite_mutex;
std::unordered_map<u64, CompositeShader>* composites = new std::unordered_map<u64, CompositeShader>;

/// The variant's HDR shader, or why there is none.
std::optional<std::vector<u32>> BuildComposite(const TonemapVariant& v, std::span<const u32> game,
                                               std::string* why) {
    const SpvInterface in = ReadInterface(game);
    // The constant buffer: the game shader's only buffer (several ssbo_N aliases share it).
    std::optional<u32> buffer_index, buffer_binding;
    for (const auto& [name, ids] : in.ids) {
        if (name.rfind("ssbo_", 0) != 0) {
            continue;
        }
        const u32 index = u32(std::strtoul(name.c_str() + 5, nullptr, 10));
        if (buffer_index && *buffer_index != index) {
            *why = "several buffers";
            return std::nullopt;
        }
        buffer_index = index;
        buffer_binding = in.Binding(name);
    }
    if (!buffer_index || !buffer_binding) {
        *why = "no constant buffer";
        return std::nullopt;
    }
    if (in.ids.contains(fmt::format("fs_img{}", int(v.img_scene))) == false ||
        in.ids.contains(fmt::format("fs_samp{}", int(v.img_scene))) == false) {
        *why = "unexpected images";
        return std::nullopt;
    }
    // One sampler per image, in the same order (each fetch uses its own S#).
    u32 images = 0, samplers = 0;
    while (in.ids.contains(fmt::format("fs_img{}", images))) {
        ++images;
    }
    while (in.ids.contains(fmt::format("fs_samp{}", samplers))) {
        ++samplers;
    }
    if (images != samplers) {
        *why = fmt::format("{} images but {} samplers", images, samplers);
        return std::nullopt;
    }
    const auto image = [&](int index) { return in.Binding(fmt::format("fs_img{}", index)); };
    const auto sampler = [&](int index) { return in.Binding(fmt::format("fs_samp{}", index)); };
    // Inputs: interpolated per vertex (barycentrics), as the HDR shader declares them.
    const auto input = [&](int attr) { return in.Location(fmt::format("fs_in_attr{}_p", attr)); };
    const auto scene_loc = input(v.attr_scene);
    const auto mult_loc = input(v.attr_mult);
    const auto bloom_loc = input(v.attr_bloom);
    const auto vig_loc = v.attr_vig >= 0 ? input(v.attr_vig) : scene_loc;
    const auto bindings = {image(v.img_scene), image(v.img_mult), image(v.img_bloom),
                           sampler(v.img_scene), sampler(v.img_mult), sampler(v.img_bloom)};
    if (std::ranges::any_of(bindings, [](const auto& b) { return !b.has_value(); })) {
        *why = "image bindings not found";
        return std::nullopt;
    }
    if (!scene_loc || !mult_loc || !bloom_loc || !vig_loc) {
        *why = "inputs not interpolated per vertex";
        return std::nullopt;
    }

    std::vector<u32> spv(std::begin(BB_COMPOSITE_HDR_FRAG), std::end(BB_COMPOSITE_HDR_FRAG));
    const SpvInterface ours = ReadInterface(spv);
    const auto it = bindings.begin();
    const bool ok =
        SetDecoration(spv, ours, "bb_ssbo", SpvDecorationBinding, *buffer_binding) &&
        SetDecoration(spv, ours, "bb_img_scene", SpvDecorationBinding, *it[0]) &&
        SetDecoration(spv, ours, "bb_img_mult", SpvDecorationBinding, *it[1]) &&
        SetDecoration(spv, ours, "bb_img_bloom", SpvDecorationBinding, *it[2]) &&
        SetDecoration(spv, ours, "bb_samp_scene", SpvDecorationBinding, *it[3]) &&
        SetDecoration(spv, ours, "bb_samp_mult", SpvDecorationBinding, *it[4]) &&
        SetDecoration(spv, ours, "bb_samp_bloom", SpvDecorationBinding, *it[5]) &&
        SetDecoration(spv, ours, "bb_in_scene", SpvDecorationLocation, *scene_loc) &&
        SetDecoration(spv, ours, "bb_in_mult", SpvDecorationLocation, *mult_loc) &&
        SetDecoration(spv, ours, "bb_in_bloom", SpvDecorationLocation, *bloom_loc) &&
        SetDecoration(spv, ours, "bb_in_vig", SpvDecorationLocation, *vig_loc);
    if (!ok) {
        *why = "the HDR shader's decorations were not found";
        return std::nullopt;
    }
    // The channel holds the untonemapped image (the game's curve below the knee, its slope
    // continued above, up to 10000 nits); the final pass grades it and tonemaps it to the peak
    // live, with Neutwo or PsychoV30 (in-game menu), as RenoDX does.
    const float paper = float(EmulatorSettings.GetHdrPaperWhite());
    const float peak = std::max(10000.0f / paper, HdrChannel::Knee() + 0.05f);
    SetSpirvSpecConstant(spv, 1, std::bit_cast<u32>(peak));
    SetSpirvSpecConstant(spv, 2, std::bit_cast<u32>(HdrChannel::Knee()));
    SetSpirvSpecConstant(spv, 10, v.curve);
    SetSpecBool(spv, 11, v.luminance);
    SetSpirvSpecConstant(spv, 12, v.pre);
    SetSpecBool(spv, 13, v.pre_luminance);
    SetSpecBool(spv, 14, v.cos4);
    SetSpecBool(spv, 15, v.vig888);
    SetSpecBool(spv, 16, v.gamma);
    SetSpecBool(spv, 17, v.additive);
    SetSpirvSpecConstant(spv, 18, *buffer_index);
    SetSpirvSpecConstant(spv, 19, 0u); // untonemapped (the final pass tonemaps)
    SetSpecBool(spv, 20, v.gain);
    SetSpecBool(spv, 21, v.floor);
    SetSpecBool(spv, 22, v.scene_mat);
    SetSpecBool(spv, 23, v.glow_mat);
    SetSpecBool(spv, 24, v.grade);
    return spv;
}

constexpr const char* CurveNames[] = {"exp curve", "extended Reinhard", "log curve", "no curve"};
} // namespace

void HdrChannel::NoteGameShader(u64 pgm_hash, std::span<const u32> spv) {
    if (!Generic()) {
        return;
    }
    const TonemapVariant* v = FindVariant(pgm_hash);
    if (!v || !v->standard || v->additive) {
        return; // additive variants output HDR for a later pass: nothing to do here
    }
    std::string why;
    auto built = BuildComposite(*v, spv, &why);
    std::scoped_lock lock{composite_mutex};
    auto [it, fresh] = composites->try_emplace(pgm_hash);
    if (!built) {
        if (fresh) {
            LOG_WARNING(Render_Vulkan, "HDR pass: composite variant {:08x} left in SDR: {}",
                        u32(pgm_hash), why);
            it->second.conflicting = true;
        }
        return;
    }
    if (fresh) {
        it->second.spv = std::move(*built);
        LOG_INFO(Render_Vulkan, "HDR pass: composite variant {:08x} ready ({}{}, {})",
                 u32(pgm_hash), CurveNames[v->curve < 4 ? v->curve : 3],
                 v->luminance ? " on luminance" : "", "untonemapped");
    } else if (!it->second.conflicting && it->second.spv != *built) {
        it->second.conflicting = true;
        LOG_WARNING(Render_Vulkan,
                    "HDR pass: composite variant {:08x} has permutations with different "
                    "interfaces, left in SDR",
                    u32(pgm_hash));
    }
}

vk::ShaderModule HdrChannel::CompositeModule(const Instance& instance, u64 pgm_hash) {
    std::scoped_lock lock{composite_mutex};
    const auto it = composites->find(pgm_hash);
    if (it == composites->end() || it->second.conflicting || it->second.spv.empty()) {
        return {};
    }
    if (!it->second.module) {
        it->second.module = CompileSPV(it->second.spv, instance.GetDevice());
    }
    return it->second.module;
}

HdrChannel& HdrChannel::Get() {
    // Never destroyed: at exit the Vulkan device is gone before static objects, and freeing the
    // images then would crash. The process exit frees them.
    static HdrChannel* const channel = new HdrChannel();
    return *channel;
}

bool HdrChannel::Enabled() {
    static const bool enabled = (EmulatorSettings.GetHdrTonemapMode() == 3 ||
                                 EmulatorSettings.GetHdrTonemapMode() == 4) &&
                                EmulatorSettings.IsHdrAllowed();
    return enabled;
}

bool HdrChannel::Generic() {
    static const bool generic =
        EmulatorSettings.GetHdrTonemapMode() == 4 && EmulatorSettings.IsHdrAllowed();
    return generic;
}

float HdrChannel::PeakNits() {
    // bbport.ini hdr_peak_nits or BB_HDR_PEAK_NITS; the in-game menu changes it live.
    return std::clamp(BbSettings::Get().hdr_peak_nits.load(), 200.0f, 10000.0f);
}

float HdrChannel::Knee() {
    static const float knee = EnvFloat("BB_HDR_KNEE", 0.6f, 0.3f, 0.95f);
    return knee;
}

u32 HdrChannel::Tonemapper() {
    // bbport.ini hdr_tonemapper or BB_HDR_TONEMAPPER; the in-game menu changes it live (the final
    // pass tonemaps).
    return u32(std::clamp(BbSettings::Get().hdr_tonemapper.load(), 0, 1));
}

u32 HdrChannel::Debug() {
    // BB_HDR_DEBUG at start; the in-game menu changes it live.
    return u32(std::clamp(BbSettings::Get().hdr_debug.load(), 0, 2));
}

void HdrChannel::NoteDraw(u64 ps_hash) {
    const TonemapVariant* v = FindVariant(ps_hash);
    if (!v) {
        return;
    }
    // GPU thread only: plain flags are enough.
    static std::array<bool, TonemapVariants.size()> seen{};
    static u32 count = 0;
    bool& flag = seen[size_t(v - TonemapVariants.data())];
    if (flag) {
        return;
    }
    flag = true;
    ++count;
    static constexpr const char* pre[] = {"", ", sqrt exposure", ", exp exposure",
                                          ", alpha exposure"};
    std::string status;
    if (!v->standard) {
        status = "not covered (several scene samples: distortion effect)";
    } else if (v->additive) {
        status = "HDR output for a later pass (no curve here)";
    } else if (GameShaderReplaced(ps_hash)) {
        status = "replaced by bbport";
    } else if (Generic()) {
        std::scoped_lock lock{composite_mutex};
        const auto it = composites->find(ps_hash);
        status = it != composites->end() && !it->second.conflicting ? "HDR pass"
                                                                    : "NOT covered: stays SDR";
    } else {
        status = "NOT replaced: no HDR highlights here";
    }
    LOG_INFO(Render_Vulkan, "HDR tonemap variant {:08x} in use ({} so far): {}{}{}{}{}, {}{}; {}",
             v->hash, count, v->standard ? CurveNames[v->curve < 4 ? v->curve : 3] : "special",
             v->luminance ? " on luminance" : "", pre[v->pre < 4 ? v->pre : 0],
             v->cos4 ? ", cos4 vignette" : "", v->vig888 ? ", lens vignette" : "",
             v->gamma ? "gamma 2.2" : "linear", v->grain ? ", grain" : "", status);
}

vk::ShaderModule HdrChannel::FragmentModule(const Instance& instance) {
    std::scoped_lock lock{mutex};
    if (!fragment) {
        std::vector<u32> spv(std::begin(BB_TONEMAP_HDR_FRAG), std::end(BB_TONEMAP_HDR_FRAG));
        const float paper = float(EmulatorSettings.GetHdrPaperWhite());
        // Untonemapped too (the final pass tonemaps to the peak).
        const float peak = std::max(10000.0f / paper, Knee() + 0.05f);
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
    publishes.fetch_add(1, std::memory_order_release);
}

void HdrChannel::NoteFullscreen(u64 vs_hash, u64 ps_hash, u32 w, u32 h, u32 targets) {
    // GPU thread only.
    static std::unordered_set<u64> seen;
    if (seen.size() >= 400) {
        return;
    }
    const u64 key = (vs_hash << 32) ^ (ps_hash & 0xffffffffu);
    if (!seen.insert(key).second) {
        return;
    }
    LOG_INFO(Render_Vulkan, "HDR scan: full-screen pass vs {:08x} ps {:08x} into {}x{} ({} targets){}",
             vs_hash, ps_hash, w, h, targets, FindVariant(ps_hash) ? ", composite variant" : "");
}

std::optional<HdrChannel::Latest> HdrChannel::LatestImage() const {
    std::scoped_lock lock{mutex};
    const s32 index = latest.load(std::memory_order_acquire);
    // Called once per presented frame. When the game stops drawing a known composite (an area
    // whose composite is not covered), the last image would be stale: gains and the scene / UI
    // test of another place. After two frames without a new one, no channel (SDR).
    const u32 count = publishes.load(std::memory_order_acquire);
    if (count != seen_publishes) {
        seen_publishes = count;
        stale_frames = 0;
    } else if (stale_frames < 1000) {
        ++stale_frames;
    }
    if (stale_frames > 2 || index < 0 || !images[index].view) {
        return std::nullopt;
    }
    return Latest{*images[index].view, width, height};
}

} // namespace Vulkan
