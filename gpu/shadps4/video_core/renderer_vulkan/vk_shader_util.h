// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

/**
 * @brief Creates a vulkan shader module from SPIR-V bytecode.
 * @param code The SPIR-V bytecode data.
 * @param device The vulkan device handle
 */
vk::ShaderModule CompileSPV(std::span<const u32> code, vk::Device device);

/// bbport HDR: the SPIR-V to compile for a game shader: bbport's replacement for the shaders it
/// replaces (Bloodborne's tonemap), when enabled and when the original is the expected one;
/// otherwise the original.
std::span<const u32> GameShaderCode(u64 pgm_hash, std::span<const u32> original);

} // namespace Vulkan
