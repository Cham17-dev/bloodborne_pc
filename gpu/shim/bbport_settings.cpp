// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace BbSettings {

namespace {

// The HDR presets' settings (hdr_preset1..3 in bbport.ini: "key=value;key=value..."), and
// whether the file had a preset choice (bbport.ini from before the presets).
std::mutex hdr_slot_mutex;
std::array<std::string, HdrPresetCount - 1> hdr_slots;
bool loaded_hdr_preset = false;
// The display's peak (SetDisplayPeak), and whether bbport.ini set the peak and the game's white.
std::atomic<float> display_peak{0.0f};
bool loaded_peak = false, loaded_paper_white = false;

const char* Path() {
    const char* env = std::getenv("BB_CONFIG");
    return env && env[0] ? env : "bbport.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "frame_gen") {
        for (int g = 0; g < FrameGenCount; ++g) {
            if (value == FrameGenName(g)) {
                v.frame_gen = g;
            }
        }
    } else if (key == "frame_gen_multiplier") {
        v.frame_gen_multiplier = std::clamp(i, 2, 4);
    } else if (key == "frame_gen_fit_display") {
        v.frame_gen_fit_display = i != 0;
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "hdr_paper_white") {
        if (!value.empty()) v.hdr_paper_white = Clamp(f, 80.0f, 500.0f);
        loaded_paper_white = loaded_paper_white || !value.empty();
    } else if (key == "hdr_peak_nits") {
        if (!value.empty()) v.hdr_peak_nits = Clamp(f, 200.0f, 10000.0f);
        loaded_peak = loaded_peak || !value.empty();
    } else if (key == "hdr_strength") {
        if (!value.empty()) v.hdr_strength = Clamp(f, 0.0f, 3.0f);
    } else if (key == "hdr_boost") {
        if (!value.empty()) v.hdr_boost = Clamp(f, 0.0f, 1.0f);
    } else if (key == "hdr_tonemapper") {
        if (!value.empty()) v.hdr_tonemapper = std::clamp(i, 0, HdrToneMapperPsychoScene);
    } else if (key == "hdr_preset") {
        if (!value.empty()) v.hdr_preset = std::clamp(i, 0, HdrPresetCount - 1);
        loaded_hdr_preset = true;
    } else if (key == "hdr_settings_mode") {
        if (!value.empty()) v.hdr_settings_mode = std::clamp(i, 0, 2);
    } else if (key.size() == 11 && key.starts_with("hdr_preset") && key[10] >= '1' &&
               key[10] <= '0' + HdrPresetCount - 1) {
        std::scoped_lock lock{hdr_slot_mutex};
        hdr_slots[key[10] - '1'] = value;
    } else if (key == "hdr_scaling") {
        v.hdr_per_channel = value == "per_channel";
    } else if (key == "hdr_ui_nits") {
        if (!value.empty()) v.hdr_ui_nits = Clamp(f, 48.0f, 500.0f);
    } else if (key == "hdr_gamma") {
        v.hdr_gamma22 = value != "srgb";
    } else if (key == "hdr_exposure") {
        if (!value.empty()) v.hdr_exposure = Clamp(f, 0.0f, 2.0f);
    } else if (key == "hdr_contrast") {
        if (!value.empty()) v.hdr_contrast = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_highlights") {
        if (!value.empty()) v.hdr_grade_highlights = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_shadows") {
        if (!value.empty()) v.hdr_shadows = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_saturation") {
        if (!value.empty()) v.hdr_saturation = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_highlight_saturation") {
        if (!value.empty()) v.hdr_highlight_saturation = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_blowout") {
        if (!value.empty()) v.hdr_blowout = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_bloom") {
        if (!value.empty()) v.hdr_bloom = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_scene_grading") {
        if (!value.empty()) v.hdr_scene_grading = Clamp(f, 0.0f, 100.0f);
    } else if (key == "hdr_knee") {
        if (!value.empty()) v.hdr_knee = Clamp(f, 10.0f, 95.0f);
    } else if (key == "hdr_black_floor") {
        if (!value.empty()) v.hdr_black_floor = Clamp(f, 0.0f, 2.0f);
    } else if (key == "hdr_vignette") {
        if (!value.empty()) v.hdr_vignette = Clamp(f, 0.0f, 100.0f);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "mute_unfocused") {
        v.mute_unfocused = i != 0;
    } else if (key == "button_prompts") {
        v.button_prompts = value == "playstation" ? PromptsPlayStation
                         : value == "keyboard"    ? PromptsKeyboard
                         : value == "xbox"        ? PromptsXbox
                         : value == "switch"      ? PromptsSwitch
                                                  : PromptsAuto;
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "hud_position") {
        v.hud_position = std::clamp(i, 0, 100);
    } else if (key == "fov") {
        v.fov = std::clamp(i, FovMin, FovMax);
    } else if (key == "output_res") {
        for (int r = 0; r < OutputCount; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

// The HDR settings a preset holds, as bbport.ini writes them, separated by `sep`.
std::string HdrText(const Values& v, char sep) {
    std::string out;
    char item[64];
    const auto add = [&](const char* format, auto... args) {
        std::snprintf(item, sizeof(item), format, args...);
        out += item;
        out += sep;
    };
    add("hdr_paper_white=%.0f", v.hdr_paper_white.load());
    add("hdr_peak_nits=%.0f", v.hdr_peak_nits.load());
    add("hdr_strength=%.2f", v.hdr_strength.load());
    add("hdr_boost=%.2f", v.hdr_boost.load());
    add("hdr_tonemapper=%d", v.hdr_tonemapper.load());
    add("hdr_scaling=%s", v.hdr_per_channel ? "per_channel" : "luminance");
    add("hdr_ui_nits=%.0f", v.hdr_ui_nits.load());
    add("hdr_gamma=%s", v.hdr_gamma22 ? "2.2" : "srgb");
    add("hdr_exposure=%.2f", v.hdr_exposure.load());
    add("hdr_contrast=%.0f", v.hdr_contrast.load());
    add("hdr_highlights=%.0f", v.hdr_grade_highlights.load());
    add("hdr_shadows=%.0f", v.hdr_shadows.load());
    add("hdr_saturation=%.0f", v.hdr_saturation.load());
    add("hdr_highlight_saturation=%.0f", v.hdr_highlight_saturation.load());
    add("hdr_blowout=%.0f", v.hdr_blowout.load());
    add("hdr_bloom=%.0f", v.hdr_bloom.load());
    add("hdr_vignette=%.0f", v.hdr_vignette.load());
    add("hdr_scene_grading=%.0f", v.hdr_scene_grading.load());
    add("hdr_black_floor=%.2f", v.hdr_black_floor.load());
    add("hdr_knee=%.0f", v.hdr_knee.load());
    return out;
}

// A preset's text applied to the settings (HdrText with ';').
void ApplyHdrText(Values& v, const std::string& text) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        if (const auto eq = item.find('='); eq != std::string::npos &&
                                           item.compare(0, 4, "hdr_") == 0) {
            Set(v, item.substr(0, eq), item.substr(eq + 1));
        }
        start = end + 1;
    }
}

void ResetHdr(Values& v) {
    v.hdr_paper_white = DefaultGameNits();
    v.hdr_peak_nits = DefaultPeakNits();
    v.hdr_strength = 1.0f;
    v.hdr_boost = 0.0f;
    v.hdr_tonemapper = 0;
    v.hdr_per_channel = false;
    v.hdr_ui_nits = 203.0f;
    v.hdr_gamma22 = true;
    v.hdr_exposure = 1.0f;
    v.hdr_contrast = 50.0f;
    v.hdr_grade_highlights = 50.0f;
    v.hdr_shadows = 50.0f;
    v.hdr_saturation = 50.0f;
    v.hdr_highlight_saturation = 50.0f;
    v.hdr_blowout = 0.0f;
    v.hdr_bloom = 50.0f;
    v.hdr_vignette = 50.0f;
    v.hdr_scene_grading = 100.0f;
    v.hdr_black_floor = 0.0f;
    v.hdr_knee = 60.0f;
}

} // namespace

void SelectHdrPreset(int preset) {
    auto& v = Get();
    preset = std::clamp(preset, 0, HdrPresetCount - 1);
    const int previous = v.hdr_preset;
    if (preset == previous) {
        return;
    }
    std::string text;
    {
        std::scoped_lock lock{hdr_slot_mutex};
        if (previous > 0) {
            hdr_slots[previous - 1] = HdrText(v, ';');
        }
        if (preset > 0) {
            text = hdr_slots[preset - 1];
        }
    }
    if (preset > 0) {
        ResetHdr(v); // a preset never saved starts from the defaults; a saved one sets them all
        ApplyHdrText(v, text);
    }
    v.hdr_preset = preset;
}

void ResetHdr() {
    ResetHdr(Get());
}

float DisplayPeak() {
    return display_peak.load();
}

float DefaultPeakNits() {
    const float peak = display_peak.load();
    return peak > 0.0f ? std::clamp(std::round(peak), 200.0f, 10000.0f) : 1000.0f;
}

float DefaultGameNits() {
    // RenoDX (utils/swapchain.hpp ComputeReferenceWhite): BT.2100 HLG reference white, 75 % HLG
    // through the HLG OOTF for this peak (203 nits at 1000), here at most 203 as RenoDX's default.
    const double peak = DefaultPeakNits();
    const double hlg_at_75_percent = 0.26496256042100718;
    const double gamma = 1.2 + 0.42 * std::log10(peak / 1000.0);
    const double white = peak * std::pow(hlg_at_75_percent, gamma - 1.0) * hlg_at_75_percent;
    return std::min(std::clamp(std::round(float(white)), 100.0f, 300.0f), 203.0f);
}

void SetDisplayPeak(float nits) {
    display_peak = nits > 0.0f ? nits : 0.0f;
    auto& v = Get();
    // No choice in bbport.ini yet (a first start): the display's own values.
    if (!loaded_peak) {
        v.hdr_peak_nits = DefaultPeakNits();
    }
    if (!loaded_paper_white) {
        v.hdr_paper_white = DefaultGameNits();
    }
    std::printf("HDR: the display reports a peak of %.0f nits (Windows); defaults: peak %.0f, game "
                "brightness %.0f nits\n",
                display_peak.load(), DefaultPeakNits(), DefaultGameNits());
}

Values& Get() {
    static Values values;
    return values;
}

void Load() {
    auto& v = Get();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    loaded_hdr_preset = false;
    loaded_peak = loaded_paper_white = false;
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[2048]; // the HDR presets' lines are long
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // bbport.ini from before the HDR presets: the HDR settings it has become Preset #1.
    if (!loaded_hdr_preset) {
        v.hdr_preset = 1;
        std::scoped_lock lock{hdr_slot_mutex};
        if (hdr_slots[0].empty()) {
            hdr_slots[0] = HdrText(v, ';');
        }
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("BB_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_HDR_PAPER_WHITE", "hdr_paper_white"}, {"BB_HDR_PEAK_NITS", "hdr_peak_nits"},
        {"BB_FRAME_GEN", "frame_gen"},              {"BB_FRAME_GEN_MULTIPLIER", "frame_gen_multiplier"},
        {"BB_FRAME_GEN_FIT_DISPLAY", "frame_gen_fit_display"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    if (const char* env = std::getenv("BB_HDR_TONEMAPPER")) {
        v.hdr_tonemapper = std::clamp(std::atoi(env), 0, HdrToneMapperPsychoScene);
    }
    if (const char* env = std::getenv("BB_HDR_GAMMA22")) {
        v.hdr_gamma22 = std::atoi(env) != 0;
    }
    if (const char* env = std::getenv("BB_HDR_DEBUG")) {
        v.hdr_debug = std::clamp(std::atoi(env), 0, 2);
    }
    if (const char* env = std::getenv("BB_HDR_TEST_PATTERN")) {
        v.hdr_test_pattern = std::atoi(env) != 0;
    }
    v.startup_preset = v.preset;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    v.startup_output_res = v.output_res;
    v.startup_hud_position = v.hud_position;
    v.startup_fov = v.fov;
    v.startup_live_resolution = v.live_resolution;
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    const int requested = v.upscaler;
    if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "bbport_dlss.dll or nvngx_dlss.dll missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("BB_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart. Ultrawide
    // outputs patch the camera and HUD at startup.
    if (Ultrawide(v.output_res) != Ultrawide(v.startup_output_res)) return true;
    if (Ultrawide(v.output_res) && v.hud_position != v.startup_hud_position) return true;
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    FILE* file = std::fopen(Path(), "w");
    if (!file) {
        std::printf("Settings: cannot write %s\n", Path());
        return;
    }
    std::fprintf(file,
                 "# bbport settings (in-game menu: Insert / L3+R3)\n"
                 "upscaler=%s\npreset=%d\nsharpen=%d\nsharpness=%.2f\njitter=%d\n"
                 "reactive=%d\nobject_motion=%d\nreactive_scale=%.2f\nreactive_threshold=%.2f\nreactive_max=%.2f\n"
                 "debug_view=%d\nshow_fps=%d\nfsr4_auto_exposure=%d\nfsr4_invert_jitter=%d\n",
                 UpscalerName(v.upscaler), v.preset.load(), int(v.sharpen.load()),
                 v.sharpness.load(), int(v.jitter.load()), int(v.reactive.load()),
                 int(v.object_motion.load()),
                 v.reactive_scale.load(), v.reactive_threshold.load(), v.reactive_max.load(),
                 v.debug_view.load(), int(v.show_fps.load()),
                 int(v.fsr4_auto_exposure.load()), int(v.fsr4_invert_jitter.load()));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        std::fprintf(file, "%s=%d\n", Effects[e].key, int(v.effects[e].load()));
    }
    // The HDR settings in use (the selected preset's), and the presets.
    std::fputs(HdrText(v, '\n').c_str(), file);
    std::fprintf(file, "hdr_preset=%d\nhdr_settings_mode=%d\n", v.hdr_preset.load(),
                 v.hdr_settings_mode.load());
    {
        std::scoped_lock lock{hdr_slot_mutex};
        if (const int preset = v.hdr_preset; preset > 0) {
            hdr_slots[preset - 1] = HdrText(v, ';');
        }
        for (size_t s = 0; s < hdr_slots.size(); ++s) {
            if (!hdr_slots[s].empty()) {
                std::fprintf(file, "hdr_preset%zu=%s\n", s + 1, hdr_slots[s].c_str());
            }
        }
    }
    std::fprintf(file, "frame_gen=%s\nframe_gen_multiplier=%d\nframe_gen_fit_display=%d\n",
                 FrameGenName(v.frame_gen), v.frame_gen_multiplier.load(),
                 int(v.frame_gen_fit_display.load()));
    std::fprintf(file, "model_lod=%d\noutput_res=%dx%d\n", v.model_lod.load(),
                 OutputWidths[v.output_res], OutputHeights[v.output_res]);
    std::fprintf(file, "mute_unfocused=%d\n", int(v.mute_unfocused.load()));
    static constexpr const char* prompts[PromptsCount] = {"auto", "playstation", "keyboard", "xbox", "switch"};
    std::fprintf(file, "button_prompts=%s\n", prompts[std::clamp(v.button_prompts.load(), 0, PromptsCount - 1)]);
    // Read by run.py at start (hud_layout.py).
    std::fprintf(file, "hud_position=%d\n", v.hud_position.load());
    // Read by run.py at start (fov.py).
    std::fprintf(file, "fov=%d\n", v.fov.load());
    // Read by run.sh at start.
    std::fprintf(file, "live_resolution=%s\n", v.live_resolution < 0 ? "auto"
                                                  : v.live_resolution ? "1" : "0");
    std::fclose(file);
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* FrameGenName(int frame_gen) {
    static constexpr const char* names[FrameGenCount] = {"off", "fsr3", "dlss"};
    return names[std::clamp(frame_gen, 0, FrameGenCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

} // namespace BbSettings
