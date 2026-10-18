// SPDX-License-Identifier: GPL-2.0-or-later
// A file of its own: <windows.h> and DXGI stay away from the Vulkan headers.
#include "display_peak.h"

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_6.h>
#endif

namespace BbDisplay {

float PeakNits(void* hwnd) {
#ifdef _WIN32
    if (!hwnd) {
        return 0.0f;
    }
    const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY);
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return 0.0f;
    }
    float peak = 0.0f;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; peak == 0.0f && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++a) {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; peak == 0.0f && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND;
             ++o) {
            DXGI_OUTPUT_DESC desc{};
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor &&
                SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6),
                                                 reinterpret_cast<void**>(&output6)))) {
                DXGI_OUTPUT_DESC1 desc1{};
                // The current display color space, not the swapchain's: HDR on in Windows.
                if (SUCCEEDED(output6->GetDesc1(&desc1)) &&
                    desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                    peak = desc1.MaxLuminance;
                }
                output6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return peak > 0.0f ? peak : 0.0f;
#else
    (void)hwnd;
    return 0.0f;
#endif
}

} // namespace BbDisplay
