// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <span>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <algorithm>

#include <SDL3/SDL.h>
#include "bbport_settings.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/host_shaders/overlay_hdr_frag.h"

// DejaVu Sans (Cyrillic), embedded (third_party/fonts, Bitstream Vera license).
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
#endif
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
// Settings changed in the menu: saved on close, or a second after the last change while it stays
// open (quitting the game with the menu open lost them).
bool dirty = false;
std::chrono::steady_clock::time_point dirty_time{};
// The game's text dialog (SetTextEntry), guarded by imgui_mutex.
bool text_entry_active = false;
std::string text_entry_prompt, text_entry_text;
float base_scale = 1.0f;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f, last_present_ms = 0.0f;

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    ImGui::GetIO().MouseDrawCursor = value;
    if (!value && dirty) {
        dirty = false;
        BbSettings::Save();
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them. `value` is taken by reference: in
// Store(x, v, ImGui::Slider(&v)) the slider runs before Store reads v, whatever the order in
// which the arguments are evaluated (a copy could be the value from before the slider moved it,
// which put paper white, peak, HUD brightness and the grading sliders back at once).
template <typename T>
void Store(std::atomic<T>& target, const T& value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
        dirty_time = std::chrono::steady_clock::now();
    }
}

void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    const bool changed = ImGui::Checkbox(label, &v);
    Store(value, v, changed);
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi) {
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, "%.2f");
    Store(value, v, changed);
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

const char* HdrCompareName(int mode) {
    static constexpr const char* names[] = {"HDR", "SDR (the game's image)",
                                             "Split: SDR left | HDR right"};
    return names[std::clamp(mode, 0, int(BbSettings::Values::HdrCompareCount) - 1)];
}

// A short message at the top of the screen (F9), for two seconds.
std::string toast_text;
std::chrono::steady_clock::time_point toast_until{};

void ShowToast(std::string text) {
    toast_text = std::move(text);
    toast_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

bool ToastActive() {
    return !toast_text.empty() && std::chrono::steady_clock::now() < toast_until;
}

void CycleHdrCompare() {
    auto& s = BbSettings::Get();
    if (!s.hdr_output) {
        return;
    }
    const int next = (s.hdr_compare + 1) % int(BbSettings::Values::HdrCompareCount);
    s.hdr_compare = next;
    ShowToast(std::string("HDR comparison: ") + HdrCompareName(next) + "  (F9)");
}

// A label in a corner of the screen, no input.
void Label(const char* id, ImVec2 pos, ImVec2 pivot, const char* text) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(text);
    ImGui::End();
}

// The toast, and the SDR / HDR labels of the split comparison.
void HdrLabels() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    const float top = viewport->WorkPos.y + 60.0f * base_scale;
    const float mid = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
    if (ToastActive()) {
        Label("##hdr_toast", ImVec2(mid, viewport->WorkPos.y + pad), ImVec2(0.5f, 0.0f),
              toast_text.c_str());
    }
    const auto& s = BbSettings::Get();
    if (s.hdr_output && s.hdr_compare == BbSettings::Values::HdrCompareSplit) {
        Label("##hdr_sdr", ImVec2(mid - pad, top), ImVec2(1.0f, 0.0f), "SDR (game)");
        Label("##hdr_hdr", ImVec2(mid + pad, top), ImVec2(0.0f, 0.0f), "HDR");
    }
}

// Marks the settings changed without a widget (preset switches, Reset all): saved like the rest.
void MarkDirty() {
    dirty = true;
    dirty_time = std::chrono::steady_clock::now();
}

// A slider with a reset button after it (as in ReShade and RenoDX), then its hint.
void SliderReset(const char* label, std::atomic<float>& value, float lo, float hi, float reset,
                 const char* format, const char* hint) {
    ImGui::PushID(label);
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, format);
    Store(value, v, changed);
    ImGui::SameLine();
    ImGui::BeginDisabled(std::abs(value.load() - reset) < 1e-4f);
    if (ImGui::SmallButton("Reset")) {
        Store(value, reset, true);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (hint) {
        Hint(hint);
    }
}

// A combo over `names`; `current` and the result are indices into it.
int ComboIndex(const char* label, int current, std::span<const char* const> names) {
    int chosen = current;
    if (ImGui::BeginCombo(label, names[std::clamp(current, 0, int(names.size()) - 1)])) {
        for (int i = 0; i < int(names.size()); ++i) {
            if (ImGui::Selectable(names[i], i == current)) {
                chosen = i;
            }
        }
        ImGui::EndCombo();
    }
    return chosen;
}

// What runs now: the HDR mode, and whether this area gets its HDR highlights.
void HdrStatus(const BbSettings::Values& s) {
    const int mode = s.hdr_mode;
    if (!s.hdr_highlights) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "HDR highlights off: the game's image in HDR10 (launcher: HDR mode).");
    } else if (mode == 5) {
        if (s.hdr_native16_live) {
            ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "HDR 16-bit: active");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "HDR 16-bit: not active here (this area is shown in SDR)");
        }
    } else if (s.hdr_channel_live) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "HDR highlights: active here");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "HDR highlights: not in this area (shown in SDR)");
    }
}

// The HDR settings, laid out as a RenoDX mod's: Preset (Off and three of the player's), Settings
// Mode, then Tone Mapping, Color Grading and Effects; bbport's own sliders and the test tools in
// Advanced only.
void HdrSection(BbSettings::Values& s) {
    ImGui::SeparatorText("HDR");
    HdrStatus(s);

    // Preset and Settings Mode: sliders showing the choice's name, as RenoDX's.
    static constexpr const char* presets[] = {"Off", "Preset #1", "Preset #2", "Preset #3"};
    const int preset = std::clamp(s.hdr_preset.load(), 0, BbSettings::HdrPresetCount - 1);
    int preset_slider = preset;
    // "##hdr": the upscaler's Preset above has the same label (same ImGui ID otherwise).
    if (ImGui::SliderInt("Preset##hdr", &preset_slider, 0, BbSettings::HdrPresetCount - 1,
                         presets[preset], ImGuiSliderFlags_AlwaysClamp) &&
        preset_slider != preset) {
        BbSettings::SelectHdrPreset(preset_slider);
        MarkDirty();
    }
    Hint("Off: the game's own look in HDR10, at 203 nits. Preset #1 to #3: your own settings, each saved on its own; the settings below change the one selected.");

    static constexpr const char* modes[] = {"Simple", "Intermediate", "Advanced"};
    const int level = std::clamp(s.hdr_settings_mode.load(), 0, 2);
    int level_slider = level;
    if (ImGui::SliderInt("Settings Mode", &level_slider, 0, 2, modes[level],
                         ImGuiSliderFlags_AlwaysClamp) &&
        level_slider != level) {
        Store(s.hdr_settings_mode, level_slider, true);
    }

    ImGui::BeginDisabled(preset == 0);
    ImGui::SeparatorText("Tone Mapping");
    // Shown as RenoDX's order; stored as bbport.ini's hdr_tonemapper (0 Neutwo, 1 PsychoV30,
    // 2 Vanilla).
    static constexpr const char* tonemappers[] = {"Vanilla", "Neutwo", "PsychoV30"};
    static constexpr int stored[] = {BbSettings::HdrToneMapperVanilla, 0, 1};
    const int tonemapper = std::clamp(s.hdr_tonemapper.load(), 0, BbSettings::HdrToneMapperVanilla);
    const int shown = tonemapper == BbSettings::HdrToneMapperVanilla ? 0 : tonemapper + 1;
    if (const int chosen = ComboIndex("Tone Mapper", shown, tonemappers); chosen != shown) {
        Store(s.hdr_tonemapper, stored[chosen], true);
    }
    Hint("Vanilla: the game's own look (no HDR highlights). Neutwo: the game's image, its light above white rolled off near your display's peak. PsychoV30: models the eye's response, truer colors in bright lights.");
    // Defaults as RenoDX's: the display's peak Windows reports, and the reference white for it.
    const float display_peak = BbSettings::DisplayPeak();
    char peak_hint[160];
    if (display_peak > 0.0f) {
        std::snprintf(peak_hint, sizeof(peak_hint),
                      "The brightest your display can show. Windows reports %.0f nits for it: the "
                      "default.", display_peak);
    } else {
        std::snprintf(peak_hint, sizeof(peak_hint),
                      "The brightest your display can show (Windows does not report it: 1000 by "
                      "default).");
    }
    SliderReset("Peak Brightness", s.hdr_peak_nits, 400.0f, 4000.0f, BbSettings::DefaultPeakNits(),
                "%.0f nits", peak_hint);
    SliderReset("Game Brightness", s.hdr_paper_white, 80.0f, 500.0f, BbSettings::DefaultGameNits(),
                "%.0f nits", "Brightness of the game's white: by default the HDR reference for your display's peak (203 nits at 1000).");
    SliderReset("UI Brightness", s.hdr_ui_nits, 48.0f, 500.0f, 203.0f, "%.0f nits",
                "Brightness of the HUD and menus.");
    if (level >= 1) {
        static constexpr const char* gammas[] = {"Off (sRGB)", "2.2"};
        const int gamma = s.hdr_gamma22 ? 1 : 0;
        if (const int chosen = ComboIndex("Gamma Correction", gamma, gammas); chosen != gamma) {
            Store(s.hdr_gamma22, chosen == 1, true);
        }
        Hint("2.2: the game's image as SDR monitors show it, deeper shadows (default). Off: the sRGB curve, lighter near-blacks.");
        ImGui::BeginDisabled(tonemapper != 0);
        static constexpr const char* scalings[] = {"Luminance", "Per Channel"};
        const int scaling = s.hdr_per_channel ? 1 : 0;
        if (const int chosen = ComboIndex("Scaling", scaling, scalings); chosen != scaling) {
            Store(s.hdr_per_channel, chosen == 1, true);
        }
        Hint("Neutwo only. Luminance keeps the colors of bright lights; Per Channel lets them go toward white sooner (a filmic look).");
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Color Grading");
    const auto percent = [](const char* label, std::atomic<float>& value, float reset,
                            const char* hint) {
        SliderReset(label, value, 0.0f, 100.0f, reset, "%.0f", hint);
    };
    SliderReset("Exposure", s.hdr_exposure, 0.0f, 2.0f, 1.0f, "%.2f", nullptr);
    percent("Highlights", s.hdr_grade_highlights, 50.0f, nullptr);
    percent("Shadows", s.hdr_shadows, 50.0f, nullptr);
    percent("Contrast", s.hdr_contrast, 50.0f, nullptr);
    percent("Saturation", s.hdr_saturation, 50.0f, nullptr);
    if (level >= 1) {
        percent("Highlight Saturation", s.hdr_highlight_saturation, 50.0f, nullptr);
        percent("Blowout", s.hdr_blowout, 0.0f,
                "The brightest colors fade to white near the peak, like overexposed film.");
        ImGui::BeginDisabled(s.hdr_mode != 5);
        SliderReset("Color Grade Strength", s.hdr_scene_grading, 0.0f, 100.0f, 100.0f, "%.0f %%",
                    "The game's own color grading: 100 is the game's look, 0 the image before it. HDR 16-bit mode.");
        ImGui::EndDisabled();
        SliderReset("Black Floor", s.hdr_black_floor, 0.0f, 2.0f, 0.0f, "%.2f nits",
                    "Takes the game's darkest grey down to true black (OLED). With the game's brightness at 5, try 0.4.");
    }

    ImGui::SeparatorText("Effects");
    SliderReset("Bloom", s.hdr_bloom, 0.0f, 100.0f, 50.0f, "%.0f",
                "The glow around lights: 50 is the game's, 0 removes it.");
    if (level >= 1) {
        SliderReset("Vignette", s.hdr_vignette, 0.0f, 100.0f, 50.0f, "%.0f",
                    "The darkening toward the screen edges, where the game has one: 50 is the game's.");
    }

    if (level >= 2) {
        ImGui::SeparatorText("bbport Highlights");
        SliderReset("Highlight Strength", s.hdr_strength, 0.0f, 3.0f, 1.0f, "%.2f",
                    "How much of the light the game computed above its white is shown: 0 is the SDR look, 1 bbport's default.");
        ImGui::BeginDisabled(s.hdr_mode != 5);
        SliderReset("Highlight Range", s.hdr_knee, 10.0f, 95.0f, 60.0f, "from %.0f %% of white",
                    "Where the game's light starts to go above white: lower lets more of the bright parts reach HDR (the Hunter's Dream moon: 30-40). HDR 16-bit mode.");
        ImGui::EndDisabled();
        SliderReset("Highlight Boost", s.hdr_boost, 0.0f, 1.0f, 0.0f, "%.2f",
                    "Raises the parts the game drew close to its white toward the peak, for scenes lit for SDR: 0 is off.");
    }
    if (ImGui::Button("Reset all")) {
        BbSettings::ResetHdr();
        MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("The settings of the preset selected, back to the defaults.");
    ImGui::EndDisabled();

    if (level >= 2) {
        ImGui::SeparatorText("Debug");
        static constexpr const char* compares[] = {"HDR", "SDR (the game's image)",
                                                   "Split: SDR left | HDR right"};
        s.hdr_compare = ComboIndex("Compare", s.hdr_compare, compares);
        Hint("F9 switches it with the menu closed. Not saved.");
        static constexpr const char* debug_views[] = {"Off", "Highlight gain as colors",
                                                      "Pixels taken as UI in red"};
        s.hdr_debug = ComboIndex("Debug view", s.hdr_debug, debug_views);
        if (s.hdr_mode == 5) {
            static constexpr const char* decodes[] = {"Auto", "Linear", "sRGB signal"};
            s.hdr_native16_decode = ComboIndex("16-bit decoding", s.hdr_native16_decode, decodes);
            static constexpr const char* probes[] = {
                "Off", "1. Composite: HDR part", "2. Final image above white",
                "3. Color grading input above white", "4. Color grading area",
                "5. Copy above white"};
            s.hdr_native16_probe = ComboIndex("16-bit probe", s.hdr_native16_probe, probes);
            Hint("1, green where the composite gets light above white; 2, magenta where the image reaching the screen is above white; 3, magenta where the color grading reads above white (cyan: exactly white); 4, green where it draws; 5, magenta where the copies are above white. Not saved.");
            if (ImGui::Button("Log the 16-bit chain (one frame)")) {
                s.hdr_chain_trace = true;
            }
        }
        bool pattern = s.hdr_test_pattern;
        if (ImGui::Checkbox("Test pattern", &pattern)) {
            s.hdr_test_pattern = pattern;
        }
        Hint("Grey patches from 0.1 to 10000 nits: set Peak Brightness to the last one brighter than its left neighbour. Not saved.");
    }

    ImGui::SeparatorText("About");
    ImGui::TextDisabled("bbport HDR: HDR10 output and the game's own light in 16 bits.");
    ImGui::TextDisabled("Tone mappers Neutwo and PsychoV30 and this menu's layout from RenoDX");
    ImGui::TextDisabled("(github.com/clshortfuse/renodx, by ShortFuse and contributors, MIT).");
}

/// The presented frame rate; with frame generation adding frames also the game's own rate,
/// which is what the GPU's load (resolution, upscaler) changes. The total follows the game's
/// FPS cap times the multiplier while the GPU keeps up.
std::string FpsText() {
    const auto& s = BbSettings::Get();
    const float shown = frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f;
    char text[96];
    const int generated = s.frame_gen != BbSettings::FrameGenOff ? s.frame_gen_now.load() : 0;
    if (generated > 0 && s.game_fps > 0.0f) {
        std::snprintf(text, sizeof(text), "%.0f FPS (game %.0f, frame gen %dx)", shown,
                      s.game_fps.load(), int(shown / s.game_fps + 0.5f));
    } else {
        std::snprintf(text, sizeof(text), "%.0f FPS  %.1f ms", shown, frame_ms_avg);
    }
    return text;
}

void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40.0f * base_scale,
                                   viewport->WorkPos.y + 40.0f * base_scale),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(620.0f * base_scale, 0.0f), ImGuiCond_Appearing);
    // A menu taller than the screen would leave its last row (Close) off screen: the window
    // stays within the screen (below its 40 px offset) and scrolls instead.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f),
        ImVec2(FLT_MAX, std::max(viewport->WorkSize.y - 60.0f * base_scale, 200.0f * base_scale)));
    bool keep_open = true;
    if (!ImGui::Begin("Bloodborne - Settings (Insert / L3+R3)", &keep_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%s", FpsText().c_str());

    ImGui::SeparatorText("Temporal upscaling");
    static const char* upscalers[] = {"Off", "FSR 3.1", "FSR 4 (INT8)", "FSR 4.1.1 (INT8)",
                                     "TAA (native anti-aliasing)", "DLSS (NVIDIA RTX)"};
    static const char* later[] = {"XeSS"};
    int upscaler = s.upscaler;
    if (ImGui::BeginCombo("Upscaler", upscalers[upscaler])) {
        for (int i = 0; i < BbSettings::UpscalerCount; ++i) {
            const bool supported = i == BbSettings::UpscalerFsr4 ? s.fsr4_supported.load()
                : i == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                : i == BbSettings::UpscalerDlss   ? s.dlss_supported.load() : true;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(upscalers[i], i == upscaler)) {
                Store(s.upscaler, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("- unsupported by this GPU");
            }
        }
        for (const char* name : later) {
            ImGui::BeginDisabled();
            ImGui::Selectable(name, false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("- in development");
        }
        ImGui::EndCombo();
    }
    if (const char* problem = s.dlss_problem.load(); problem && upscaler == BbSettings::UpscalerDlss) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "DLSS: %s", problem);
    }
    if (const char* problem = s.fsr4_problem.load()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "FSR 4 unavailable: %s", problem);
        if (!BbSettings::IsFsr4(s.upscaler))
            ImGui::TextUnformatted("The selected mode is active. You can select FSR 4 again.");
        ImGui::PopTextWrapPos();
    }
    if (BbSettings::IsFsr4(s.upscaler)) {
        if (s.upscaler == BbSettings::UpscalerFsr411) {
            Hint("FSR 4.1.1 INT8: AMD 4.1.1 DLL model reproduced in Vulkan. One model covers Native through Performance, with another for Ultra Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).");
        } else {
            Hint("FSR 4 INT8 uses the v07 model from the AMD FidelityFX SDK. Higher quality than FSR 3.1, with more GPU work. Changing presets rebuilds the model and may pause briefly. Assets: tools/fetch_fsr4_assets.sh.");
        }
        Checkbox("FSR 4: auto exposure", s.fsr4_auto_exposure);
        Checkbox("FSR 4: invert jitter", s.fsr4_invert_jitter);
        Hint("Ghosting diagnostics: FSR 4 uses exposure to normalize color and decide when to discard history. These options apply immediately.");
    }
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    ImGui::BeginDisabled(!upscaler_on);
    ImGui::BeginDisabled(taa);
    int preset = taa ? BbSettings::NativeAA : s.preset.load();
    char preset_label[64];
    std::snprintf(preset_label, sizeof(preset_label), "%s (x%.1f)", BbSettings::PresetName(preset),
                  BbSettings::PresetScale(preset));
    if (ImGui::BeginCombo("Preset", preset_label)) {
        for (int i = 0; i < BbSettings::PresetCount; ++i) {
            char label[64];
            const float scale = BbSettings::PresetScale(i);
            const int output = s.output_res;
            std::snprintf(label, sizeof(label), "%s (x%.1f, render %dx%d)",
                          BbSettings::PresetName(i), scale,
                          int(std::lround(BbSettings::OutputWidths[output] / scale / 2) * 2),
                          int(std::lround(BbSettings::OutputHeights[output] / scale / 2) * 2));
            if (ImGui::Selectable(label, i == preset)) {
                Store(s.preset, i, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (taa) {
        ImGui::TextWrapped("TAA smooths the scene at output resolution without FSR upscaling. The saved upscaling preset returns when you select an upscaler.");
    }
    ImGui::Text("Active scene resolution: %d x %d", s.active_render_width.load(),
                s.active_render_height.load());
    if (BbSettings::FixedRenderSession()) {
        ImGui::Text("Startup preset: %s", BbSettings::PresetName(s.startup_preset));
        if (const char* automatic = std::getenv("BB_AUTO_RENDER_RES");
            automatic && automatic[0] == '1') {
            Hint("At outputs other than 1080p, the scene resolution is patched at startup. Changes to the preset or output require a restart. This saves GPU work on older GPUs. Non-16:9 outputs always use this path to keep camera and HUD geometry synchronized.");
        } else {
            Hint("BB_RENDER_RES fixes the scene resolution at startup. Remove this explicit override to allow live resolution and preset changes. Changes to aspect ratio still require a restart.");
        }
    } else {
        Hint("Native AA uses the upscaler for anti-aliasing. Other presets reduce scene resolution relative to the output. The HUD is drawn at output resolution. Preset changes apply on the next frame in a live session.");
    }
    Checkbox("Sharpening (RCAS)", s.sharpen);
    ImGui::BeginDisabled(!s.sharpen);
    Slider("Sharpness", s.sharpness, 0.0f, 2.0f);
    Hint("Up to 1 uses the upscaler sharpening pass (RCAS). Values above 1 add another RCAS pass. Ctrl+click the slider to enter an exact value.");
    ImGui::EndDisabled();
    Checkbox("Subpixel jitter", s.jitter);
    Hint("The camera shifts by a fraction of a pixel each frame so the upscaler can recover detail across multiple frames. Without jitter, it only smooths using frame history.");

    ImGui::SeparatorText("Reactive mask");
    ImGui::BeginDisabled(taa);
    Checkbox("Enable mask", s.reactive);
    Hint("Marks transparent effects such as particles and fog so the upscaler relies less on history. Reduces trails, but can increase shimmer behind those effects.");
    ImGui::BeginDisabled(!s.reactive);
    Slider("Scale", s.reactive_scale, 0.0f, 4.0f);
    Slider("Threshold", s.reactive_threshold, 0.0f, 1.0f);
    Slider("Maximum", s.reactive_max, 0.0f, 1.0f);
    bool show_mask = s.debug_view == BbSettings::DebugReactive;
    if (ImGui::Checkbox("Show mask (debug)", &show_mask)) {
        s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Checkbox("Object motion vectors", s.object_motion);
    Hint("Accurate vectors for animated objects reduce breakup on clothing and weapons during movement. Static geometry receives no extra pass. Changes require a restart.");
    bool show_motion = s.debug_view == BbSettings::DebugMotion;
    if (ImGui::Checkbox("Show motion vectors (debug)", &show_motion)) {
        s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
    }
    Hint("Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel has accurate object motion, beyond camera motion. A moving object with no blue or red/green is treated as stationary and may leave trails.");
    ImGui::EndDisabled(); // upscaler off

    if (s.hdr_output) {
        HdrSection(s);
    }

    ImGui::SeparatorText("Frame generation");
    static const char* frame_gens[] = {"Off", "FSR 3.1 (any GPU)", "DLSS (NVIDIA RTX 40/50)"};
    const int frame_gen = s.frame_gen;
    if (ImGui::BeginCombo("Frame generation", frame_gens[frame_gen])) {
        for (int i = 0; i < BbSettings::FrameGenCount; ++i) {
            const bool supported = i != BbSettings::FrameGenDlss || s.dlssg_max_multiplier > 1;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(frame_gens[i], i == frame_gen)) {
                Store(s.frame_gen, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("- unsupported by this GPU");
            }
        }
        ImGui::EndCombo();
    }
    Hint("Generates frames between two of the game's frames: 60 FPS on screen becomes 120. Needs a display with that refresh rate (120 Hz or more at 60 FPS); it pauses when the display cannot show the extra frames, and 3x/4x drop to what it can. Adds a little input latency. Menus and movies are not generated.");
    const int max_multiplier =
        s.frame_gen == BbSettings::FrameGenDlss ? s.dlssg_max_multiplier.load() : 2;
    ImGui::BeginDisabled(s.frame_gen == BbSettings::FrameGenOff || max_multiplier <= 2);
    int multiplier = std::min(s.frame_gen_multiplier.load(), max_multiplier);
    if (ImGui::SliderInt("Multiplier", &multiplier, 2, std::max(max_multiplier, 2), "%dx")) {
        Store(s.frame_gen_multiplier, multiplier, true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(s.frame_gen == BbSettings::FrameGenOff);
    Checkbox("Fit to display refresh", s.frame_gen_fit_display);
    ImGui::EndDisabled();
    Hint("On: no more frames than the display shows (an uncapped game gets fewer generated frames, and the GPU does less). Off: the multiplier as chosen; past the refresh rate the display shows the newest frames.");
    // What runs now (fitted: at most the frames the display shows between two of the game's).
    if (s.frame_gen != BbSettings::FrameGenOff && s.frame_gen_display_hz > 0) {
        const int now = std::min(s.frame_gen_now.load(), multiplier - 1);
        if (now > 0 && s.frame_gen_flip_limit > 0) {
            ImGui::TextDisabled("Now %dx on a %d Hz display; V-Sync (FIFO) caps the game at %d FPS",
                                now + 1, s.frame_gen_display_hz.load(),
                                s.frame_gen_flip_limit.load());
        } else if (now > 0) {
            ImGui::TextDisabled("Now %dx on a %d Hz display", now + 1,
                                s.frame_gen_display_hz.load());
        } else if (s.frame_gen_fit_display) {
            ImGui::TextDisabled("Paused: no room on a %d Hz display at this frame rate",
                                s.frame_gen_display_hz.load());
        } else {
            ImGui::TextDisabled("Paused: presenting fell behind (V-Sync on a slower display?)");
        }
    }
    if (const char* problem = s.frame_gen_problem.load()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", problem);
    }

    ImGui::SeparatorText("Output resolution");
    static const char* outputs[] = {"1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160",
                                    "2560 x 1080 (21:9)", "3440 x 1440 (21:9)",
                                    "3840 x 1600 (21:9)", "5120 x 2160 (21:9)",
                                    "3840 x 1080 (32:9)", "5120 x 1440 (32:9)"};
    static_assert(sizeof(outputs) / sizeof(*outputs) == BbSettings::OutputCount);
    int output = s.output_res;
    if (ImGui::BeginCombo("Output resolution", outputs[output])) {
        for (int i = 0; i < BbSettings::OutputCount; ++i) {
            if (ImGui::Selectable(outputs[i], i == output)) {
                Store(s.output_res, i, true);
            }
        }
        ImGui::EndCombo();
    }
    if (BbSettings::Ultrawide(s.output_res)) {
        int hud = s.hud_position;
        Store(s.hud_position, hud,
              ImGui::SliderInt("HUD position", &hud, 0, 100, hud == 0 ? "Centred (16:9)" :
                                                         hud == 100 ? "Screen edges" : "%d%%"));
        Hint("Ultrawide (21:9, 32:9): moves the health, items and Blood Echoes from the centred 16:9 area toward the screen edges. Changes require a restart.");
    }
    if (BbSettings::FixedRenderSession()) {
        Hint("Final image and HUD resolution. The preset controls scene resolution relative to the output. 4K Performance is approximately 1920x1080. Changes require a restart.");
    } else {
        Hint("Output and HUD resolution changes at the next frame boundary. The preset controls scene resolution relative to the output. Resizing resets upscaler history and may pause briefly.");
    }
    static const char* live_modes[] = {"Auto (based on GPU)", "Off (faster)", "On"};
    int live = s.live_resolution + 1;
    if (ImGui::BeginCombo("Live resolution changes", live_modes[live])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(live_modes[i], i == live)) {
                Store(s.live_resolution, i - 1, true);
            }
        }
        ImGui::EndCombo();
    }
    Hint("On: output and presets change without restarting, while game post-processing stays at 1080p. Off: the scene is patched to the preset resolution at startup. Auto enables the live path on capable discrete GPUs. Changes apply after restarting the game.");
    ImGui::SeparatorText("Game effects (restart required)");
    static const char* lods[] = {"Highest (-2)", "Original", "Lower (1)", "Lowest (2)"};
    static constexpr int lod_values[] = {-2, 0, 1, 2};
    int lod_index = 1;
    for (int i = 0; i < 4; ++i) {
        if (lod_values[i] == s.model_lod) lod_index = i;
    }
    if (ImGui::BeginCombo("Model detail", lods[lod_index])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(lods[i], i == lod_index)) {
                Store(s.model_lod, lod_values[i], true);
            }
        }
        ImGui::EndCombo();
    }
    int fov = s.fov;
    Store(s.fov, fov, ImGui::SliderInt("Field of view", &fov, BbSettings::FovMin, BbSettings::FovMax,
                                       fov == BbSettings::FovDefault ? "%d (game)" : "%d"));
    Hint("Vertical field of view in degrees; the game's camera has 43. Every camera is scaled by the same amount (scripts/fov.py).");
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        Checkbox(BbSettings::Effects[e].label, s.effects[e]);
    }
    Hint("Effects are applied through game patches at startup (patches/Bloodborne.xml). Motion blur and dynamic shadows can increase GPU load.");
    Hint("Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad / Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from the debug-font mod. Right touchpad: Backspace.");

    bool restart = s.object_motion != s.startup_object_motion ||
                   s.model_lod != s.startup_model_lod || s.fov != s.startup_fov ||
                   s.live_resolution != s.startup_live_resolution ||
                   BbSettings::ResolutionNeedsRestart();
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        restart |= s.effects[e] != s.startup_effects[e];
    }
    if (restart) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "Changes will apply after restarting the game");
        if (ImGui::Button("Apply and restart")) {
            BbSettings::Save();
            runtime_restart();
        }
    }

    ImGui::SeparatorText("Other");
    Checkbox("Show FPS counter", s.show_fps);
    Checkbox("Mute when the game is in the background", s.mute_unfocused);
    static const char* prompt_modes[] = {"Follow the device in use", "PlayStation", "Keyboard and mouse", "Xbox",
                                         "Nintendo Switch"};
    int prompts = s.button_prompts;
    if (ImGui::BeginCombo("Button prompts", prompt_modes[prompts])) {
        for (int i = 0; i < BbSettings::PromptsCount; ++i) {
            if (ImGui::Selectable(prompt_modes[i], i == prompts)) Store(s.button_prompts, i, true);
        }
        ImGui::EndCombo();
    }
    Hint("The game's button icons: keyboard and mouse keys from input.ini while you play with them, the controller's (PlayStation, Xbox or Switch) while you use it.");

    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        keep_open = false;
    }
    // Issues #74, #116: leaving the game from the pad (this menu opens with L3+R3). A second
    // press confirms; the window thread then closes like the window's own close button.
    // (From bbport 0.51.)
    static bool quit_armed;
    ImGui::SameLine();
    // One ID for both labels ("###bb_quit"): with a new ID the button lost the pad's or keyboard's
    // focus at the first press, which disarmed it at once.
    if (ImGui::Button(quit_armed ? "Press again to quit###bb_quit" : "Quit game###bb_quit")) {
        if (quit_armed) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        quit_armed = !quit_armed;
    }
    if (quit_armed && !ImGui::IsItemFocused() && !ImGui::IsItemHovered()) {
        quit_armed = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Settings are saved to bbport.ini");
    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

// The game's text dialog: what is typed, and how to finish (keyboard or controller).
void TextEntryBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.42f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(viewport->WorkSize.x * 0.32f, 0.0f),
                                        ImVec2(viewport->WorkSize.x * 0.8f, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##text_entry", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.45f, 1.0f), "%s", text_entry_prompt.c_str());
    ImGui::Separator();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("%s_", text_entry_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::TextDisabled("Keyboard: type, Backspace to erase, Enter = OK, Esc = cancel");
    ImGui::TextDisabled("Controller: Cross (A) = OK, Circle (B) = cancel");
    ImGui::End();
}

void FpsCounter() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    const auto& s = BbSettings::Get();
    ImGui::Text("%s  %s", FpsText().c_str(),
                s.upscaler == BbSettings::UpscalerFsr3   ? "FSR 3.1"
                : s.upscaler == BbSettings::UpscalerFsr4 ? "FSR 4"
                : s.upscaler == BbSettings::UpscalerFsr411 ? "FSR 4.1.1"
                : s.upscaler == BbSettings::UpscalerTaa ? "TAA"
                : s.upscaler == BbSettings::UpscalerDlss ? "DLSS"
                                                         : "");
    ImGui::End();
}

} // namespace

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    // bbport HDR: on the HDR10 swapchain the menu's SDR colors are encoded in PQ.
    if (format == vk::Format::eA2B10G10R10UnormPack32) {
        info.CustomShaderFragCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(OVERLAY_HDR_FRAG),
            .pCode = OVERLAY_HDR_FRAG,
        };
    }
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert or L3+R3)\n");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const bool is_open = menu_open;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat && event.key.key == SDLK_F9 &&
            BbSettings::Get().hdr_output) {
            CycleHdrCompare();
            return true;
        }
        if (down && !event.key.repeat &&
            (event.key.key == SDLK_INSERT || (is_open && event.key.key == SDLK_ESCAPE))) {
            SetOpen(event.key.key == SDLK_INSERT ? !is_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

bool Visible() {
    const auto& s = BbSettings::Get();
    return initialized && (menu_open || text_entry_active || s.show_fps || ToastActive() ||
                           (s.hdr_output && s.hdr_compare == BbSettings::Values::HdrCompareSplit));
}

bool CapturesInput() {
    return menu_open || text_entry_active;
}

void SetTextEntry(bool active, const std::string& prompt, const std::string& text) {
    std::scoped_lock lock{imgui_mutex};
    text_entry_active = active;
    text_entry_prompt = prompt;
    text_entry_text = text;
}

// Called for every presented image: the interval for the FPS readout and BB_FPS_LOG.
void NotePresent() {
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    last_present_ms = ms;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    // BB_FPS_LOG=1: presents per second and the slowest frame, every 5 seconds (test runs).
    static const bool fps_log = [] {
        const char* env = std::getenv("BB_FPS_LOG");
        return env && env[0] == '1';
    }();
    if (fps_log) {
        static auto window_start = now;
        static int frames = 0;
        static float worst = 0.0f;
        ++frames;
        worst = std::max(worst, ms < 1000.0f ? ms : 0.0f);
        const float elapsed = std::chrono::duration<float>(now - window_start).count();
        if (elapsed >= 5.0f) {
            std::printf("Frame rate: %.1f FPS, slowest frame %.1f ms\n", frames / elapsed, worst);
            window_start = now;
            frames = 0;
            worst = 0.0f;
        }
    }
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    const float ms = last_present_ms;
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (menu_open) {
        Menu();
        if (dirty && std::chrono::steady_clock::now() - dirty_time >= std::chrono::seconds(1)) {
            dirty = false;
            BbSettings::Save();
        }
    }
    if (BbSettings::Get().show_fps && !menu_open) {
        FpsCounter();
    }
    if (text_entry_active) {
        TextEntryBox();
    }
    HdrLabels();
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay
