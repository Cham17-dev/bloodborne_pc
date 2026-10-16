// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR side channel (docs/HDR.md, step 3b). Right after Bloodborne's tonemap draw, the same
// draw is replayed with hdr/bb_tonemap_hdr.frag into an RGBA16F image owned here: per pixel, the
// gain that turns the game's color into the HDR color, and the game's own output luminance. The
// final post-process multiplies the finished SDR frame by that gain where the frame is still the
// scene (not the UI drawn over it). The game's 8-bit chain (AA, UI, display copy) is untouched.

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <span>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

class Instance;
class Scheduler;

class HdrChannel {
public:
    static HdrChannel& Get();

    /// BB_HDR_TONEMAP=3 or 4 with HDR output allowed (BB_HDR=1).
    static bool Enabled();
    /// BB_HDR_TONEMAP=4: the generic pass. The game's composite shaders are not replaced; every
    /// standard variant of bb_tonemap_variants.h gets the channel draw (hdr/bb_composite_hdr.frag).
    static bool Generic();

    /// Generic pass: called with the game's SPIR-V of every shader bbport compiles or loads. For
    /// a composite variant, builds hdr/bb_composite_hdr.frag with that variant's interface
    /// (descriptor bindings, input locations, constant buffer) and options.
    static void NoteGameShader(u64 pgm_hash, std::span<const u32> spv);
    /// Generic pass: the HDR fragment shader for this composite variant (null if none).
    static vk::ShaderModule CompositeModule(const Instance& instance, u64 pgm_hash);
    /// Peak luminance in nits (BB_HDR_PEAK_NITS, 1000 by default, 200..10000).
    static float PeakNits();
    /// Fraction of the game's white below which the game's curve is kept (BB_HDR_KNEE, 0.6).
    static float Knee();
    /// Generic pass: BB_HDR_TONEMAPPER, 0 the Neutwo shoulder (default), 1 PsychoV30.
    static u32 Tonemapper();
    /// BB_HDR_DEBUG: 0 off, 1 the gain as a heat map, 2 the pixels taken as UI in red.
    static u32 Debug();

    /// GPU thread, every draw: logs once per shader the variants of the game's final composite
    /// (bb_tonemap_variants.h) that the game uses, with their curve and whether bbport replaces it.
    static void NoteDraw(u64 ps_hash);
    /// GPU thread, every draw of 6 indices or fewer (full-screen passes): logs each new
    /// (vs, ps) pair once, with the target size, to find the composite of areas not covered.
    static void NoteFullscreen(u64 vs_hash, u64 ps_hash, u32 width, u32 height, u32 targets);

    /// The HDR tonemap fragment shader, with its specialization constants set (peak, knee).
    vk::ShaderModule FragmentModule(const Instance& instance);

    struct Slot {
        u32 index;
        vk::Image image;
        vk::ImageView view;
        vk::ImageLayout layout; ///< layout before this write (both images)
        /// Second target: the game's effects as ratios, for the live sliders: rgb = color
        /// without the bloom over the color (per channel), a = without the vignettes over with.
        vk::Image aux_image;
        vk::ImageView aux_view;
    };
    /// GPU thread: the image to write for this frame, at the tonemap target's size.
    Slot Acquire(const Instance& instance, Scheduler& scheduler, u32 width, u32 height);
    /// GPU thread: the slot's write is recorded; it is the one the presenter shows next.
    void Published(u32 index);

    struct Latest {
        vk::ImageView view;
        vk::ImageView aux_view;
        u32 width;
        u32 height;
    };
    /// Presenter: the last published image (in ShaderReadOnlyOptimal), if any.
    std::optional<Latest> LatestImage() const;

private:
    struct Image {
        VideoCore::UniqueImage image;
        vk::UniqueImageView view;
        VideoCore::UniqueImage aux;
        vk::UniqueImageView aux_view;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };
    mutable std::mutex mutex;
    std::array<Image, 2> images;
    u32 width = 0, height = 0;
    u32 next = 0;
    std::atomic<s32> latest{-1};
    std::atomic<u32> publishes{0};
    mutable u32 seen_publishes = 0; ///< presenter: publish count at the last LatestImage()
    mutable u32 stale_frames = 0;   ///< presenter: frames since a new publish
    vk::UniqueShaderModule fragment;
};

} // namespace Vulkan
