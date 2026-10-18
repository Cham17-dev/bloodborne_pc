// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace BbDisplay {

/// bbport HDR: the peak luminance (nits) Windows reports for the display showing the window
/// `hwnd` (DXGI_OUTPUT_DESC1::MaxLuminance, as RenoDX reads it), only while that display is in
/// HDR (HDR10 color space); 0 when unknown, not in HDR, or not on Windows.
float PeakNits(void* hwnd);

} // namespace BbDisplay
