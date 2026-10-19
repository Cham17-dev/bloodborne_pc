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
#include <vector>

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
    /// Also true in mode 5 (the composites are built the same way).
    static bool Generic();

    // ---- BB_HDR_TONEMAP=5: the 16-bit chain (docs/HDR.md, "Chantier 16 bits") ----------------
    // The game's 8-bit targets after its composite (1920x1080 RGBA8: the composite's target, its
    // copy for the AA, and the display buffers) become RGBA16F at the port's output-size images
    // (scene proxies, UI and display images: only with a scaled output). The composite draw is
    // followed by bbport's, which adds the HDR part (hdr/bb_composite_hdr.frag, BB_OUTPUT_DELTA),
    // so the frame carries light above the game's white to the final pass.
    static bool Native16();
    /// Mode 5 with the startup-patched render size (BB_RENDER_RES, "Live resolution changes"
    /// off): the game's targets are already at the render size and the scene has no proxies, so
    /// the chain's targets get same-size RGBA16F proxies of their own (SceneTargets::OwnSize).
    static bool Native16Fixed();
    /// Mode 5 and the port's output-size images in use (set by the scene proxies, each frame).
    static bool Native16Active();
    static void SetNative16Active(bool active);
    /// GPU thread: the color target of a draw with this pixel shader (guest address, size,
    /// format): composite variants and the shaders of the chain mark their 1920x1080 RGBA8
    /// target as 16-bit from the next frame on.
    static void NoteTarget(u64 ps_hash, VAddr address, u32 width, u32 height, vk::Format format);
    /// Whether this guest color target is drawn as RGBA16F (pipeline formats, proxies, views).
    static bool Upgraded(VAddr address, u32 width, u32 height, vk::Format format);
    /// The display buffers (the game's last copy) when the port draws them at output size.
    static void SetDisplayRedirect(bool redirect);
    static bool DisplayUpgraded(VAddr address, vk::Format format);
    /// The format the game's display copy writes through (its 8-bit attachment): sRGB when the
    /// copy's output is linear (encoded by the attachment), UNORM when it is already encoded.
    static vk::Format DisplayAttachmentFormat();
    static constexpr vk::Format Native16Format = vk::Format::eR16G16B16A16Sfloat;
    /// The game's ColorGrading pass (a color table), replaced in mode 5; its strength (in-game
    /// menu, Scene grading) goes to the shader in bb_hdr.w (hdr/bb_color_grading.frag).
    static constexpr u64 ColorGradingHash = 0xed4c7668;
    /// The game's second glow, added after its tone mapping (a blur of the 960x540 glow targets,
    /// additive); in mode 5 weighted by the in-game Bloom slider (blend constant).
    static constexpr u64 GlowAddHash = 0x8c08444c;
    /// Mode 5: bbport's version of a game shader of the chain (glow source, display copy) with
    /// the game shader's bindings and locations, or empty to keep the game's.
    static std::vector<u32> ChainReplacement(u64 pgm_hash, std::span<const u32> game);
    /// Mode 5 test (in-game menu, "Log the 16-bit chain"): one frame, from a display copy to the
    /// next, where every access to the chain's targets is logged (HDR trace lines).
    static bool Trace();
    /// GPU thread, at each display copy: starts or ends the traced frame.
    static void OnDisplayCopy();
    /// Whether a texture cache image is one of the chain's 16-bit targets.
    static bool ChainImage(VAddr address, u32 width, u32 height, vk::Format format);

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
    /// Mode 5 showing the game's own picture (the menu's preset Off, tone mapper Vanilla, or
    /// Compare on SDR full screen): the chain's HDR changes are left out (no composite delta,
    /// the game's bloom, second glow and color table) so the 16-bit chain draws the game's own.
    static bool VanillaChain();
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
