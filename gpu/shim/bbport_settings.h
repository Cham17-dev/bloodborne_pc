// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: user settings changed at run time from the in-game menu (bbport_overlay.h) and kept
// in bbport.ini (BB_CONFIG overrides the path). Environment variables override the file at
// start. Readers load the atomics every frame; writers are the menu and Load().

#pragma once

#include <atomic>

namespace BbSettings {

enum Upscaler : int { UpscalerOff = 0, UpscalerFsr3 = 1, UpscalerFsr4 = 2, UpscalerFsr411 = 3,
                      UpscalerTaa = 4, UpscalerDlss = 5, UpscalerCount };
/// FSR 4 v07 or FSR 4.1.1: the same inputs, settings and placement in the frame.
inline bool IsFsr4(int upscaler) {
    return upscaler == UpscalerFsr4 || upscaler == UpscalerFsr411;
}
enum Preset : int { NativeAA = 0, Quality, Balanced, Performance, UltraPerformance, PresetCount };
enum DebugView : int { DebugNone = 0, DebugReactive = 1, DebugMotion = 2, DebugViewCount };
/// Frame generation (vk_frame_generation.h): frames generated between two of the game's.
enum FrameGen : int { FrameGenOff = 0, FrameGenFsr3 = 1, FrameGenDlss = 2, FrameGenCount };

/// Game effects switched by the community patches at start (patches.py EFFECTS): ini key,
/// menu label, default (the game's own behaviour).
struct Effect {
    const char* key;
    const char* label;
    bool default_on;
};
inline constexpr Effect Effects[] = {
    {"effect_chromatic_aberration", "Chromatic aberration", true},
    {"effect_dof", "Depth of field (DoF)", true},
    {"effect_motion_blur", "Motion blur", true},
    {"effect_ssao", "Ambient occlusion (SSAO)", true},
    {"effect_game_aa", "Game anti-aliasing", true},
    {"effect_dynamic_shadows", "Dynamic shadows", true},
    {"effect_ssr", "Screen-space reflections (SSR)", false},
    {"skip_intro", "Skip intro logos and movie", false},
    {"debug_camera", "Free camera (Cross + L3)", false},
    {"debug_menu", "Debug menu (requires font files)", false},
    {"cheat_no_death", "Cheat: never die (minimum 1 HP)", false},
    {"cheat_stealth", "Cheat: enemies cannot see you", false},
    {"cheat_silent", "Cheat: enemies cannot hear you", false},
    {"cheat_rally_no_decay", "Cheat: Rally never decays", false},
    {"cheat_enemy_control", "Cheat: control targeted enemy (R3 / L3)", false},
    {"tweak_no_rally", "Disable Rally (health recovery)", false},
    {"tweak_camera_distance", "Increase camera distance", false},
    {"tweak_no_camera_rotation", "Disable automatic camera rotation", false},
    {"tweak_easy_run", "Run with less stick tilt", false},
    {"tweak_ragdoll", "Dark Souls-style ragdoll physics", false},
    {"tweak_circle_confirm", "Japanese/Asian buttons (Circle confirms)", false},
    {"quit_to_desktop", "Quit to Desktop in the title and System menus", true},
};
inline constexpr int EffectCount = int(sizeof(Effects) / sizeof(Effects[0]));
/// Output resolutions: the upscaler's output and the UI host targets. The 16:9 ones change
/// live; the ultrawide ones need the startup patch (patches.py ultrawide_writes, run.py).
inline constexpr int OutputWidths[] = {1280, 1920, 2560, 3840, 2560, 3440, 3840, 5120, 3840, 5120};
inline constexpr int OutputHeights[] = {720, 1080, 1440, 2160, 1080, 1440, 1600, 2160, 1080, 1440};
inline constexpr int OutputCount = 10;
inline constexpr int OutputDefault = 1; ///< 1920x1080, the game's own size
/// Wider than 16:9: camera and HUD patched at startup.
inline bool Ultrawide(int output) {
    return OutputWidths[output] * 9 > OutputHeights[output] * 16;
}

/// Button prompts (bbport_prompts.cpp): the game's icons switch with the device in use, or stay.
enum Prompts : int {
    PromptsAuto = 0, PromptsPlayStation = 1, PromptsKeyboard = 2, PromptsXbox = 3, PromptsSwitch = 4, PromptsCount
};

/// Field of view (fov.py): the game's camera has 43 degrees.
inline constexpr int FovDefault = 43, FovMin = 30, FovMax = 90;

struct Values {
    std::atomic<int> upscaler{UpscalerFsr3};
    std::atomic<int> preset{NativeAA};
    std::atomic<bool> sharpen{true};
    std::atomic<float> sharpness{0.3f};
    std::atomic<bool> jitter{true};
    std::atomic<bool> reactive{false};
    std::atomic<bool> object_motion{true};
    std::atomic<float> reactive_scale{1.0f};
    std::atomic<float> reactive_threshold{0.2f};
    std::atomic<float> reactive_max{0.9f};
    std::atomic<int> debug_view{DebugNone};
    std::atomic<bool> show_fps{false};
    // HDR output (in-game menu, applied live by the final pass): SDR white and peak in nits, the
    // strength of the HDR highlights (exponent of the side channel's gains, 1 = as computed) and
    // the boost of the SDR highlights (0 none .. 1 the game's white at the peak).
    std::atomic<float> hdr_paper_white{203.0f};
    std::atomic<float> hdr_peak_nits{1000.0f};
    std::atomic<float> hdr_strength{1.0f};
    std::atomic<float> hdr_boost{0.0f};
    // HDR, like RenoDX's settings (in-game menu, live, saved): HUD brightness in nits, the SDR
    // decoding (2.2 power or sRGB), and the scene's color grading (sliders 0..100, 50 neutral;
    // exposure 0..2, 1 neutral; blowout 0 none).
    std::atomic<float> hdr_ui_nits{203.0f};
    std::atomic<bool> hdr_gamma22{true};
    std::atomic<float> hdr_exposure{1.0f};
    std::atomic<float> hdr_contrast{50.0f};
    std::atomic<float> hdr_grade_highlights{50.0f};
    std::atomic<float> hdr_shadows{50.0f};
    std::atomic<float> hdr_saturation{50.0f};
    std::atomic<float> hdr_highlight_saturation{50.0f};
    std::atomic<float> hdr_blowout{0.0f};
    std::atomic<float> hdr_bloom{50.0f};    ///< the game's bloom, 0..100, 50 as drawn
    std::atomic<float> hdr_scene_grading{100.0f}; ///< the game's color table, 0..100 % (mode 5)
    std::atomic<float> hdr_vignette{50.0f}; ///< the game's vignettes, 0..100, 50 as drawn
    std::atomic<float> hdr_black_floor{0.0f}; ///< nits of the game's black taken down to 0 (0..2)
    std::atomic<float> hdr_knee{60.0f}; ///< mode 5: % of the game's white where HDR starts
    /// Set by the presenter: the frame is sent in HDR10 (the menu shows the HDR section).
    std::atomic<bool> hdr_output{false};
    /// Set by the presenter: the all-areas HDR highlights are on (strength and boost apply).
    std::atomic<bool> hdr_highlights{false};
    // HDR tests (in-game menu, this session only; BB_HDR_DEBUG / BB_HDR_TEST_PATTERN at start).
    enum HdrCompare : int { HdrCompareHdr = 0, HdrCompareSdr = 1, HdrCompareSplit = 2,
                            HdrCompareCount };
    std::atomic<int> hdr_compare{HdrCompareHdr}; ///< F9 cycles
    std::atomic<int> hdr_debug{0};               ///< 1 gain as colors, 2 pixels taken as UI
    std::atomic<bool> hdr_test_pattern{false};
    std::atomic<int> hdr_native16_decode{0}; ///< mode 5 test: 0 auto, 1 linear, 2 sRGB signal
    std::atomic<int> hdr_native16_probe{0};  ///< mode 5 test: chain stage shown in magenta
    std::atomic<bool> hdr_chain_trace{false}; ///< mode 5 test: log one frame of the 16-bit chain
    // Set by the renderer, shown by the menu.
    std::atomic<bool> hdr_channel_live{false};   ///< this frame has HDR highlights
    std::atomic<bool> hdr_native16_live{false}; ///< this frame is the 16-bit chain's
    std::atomic<unsigned> hdr_variant{0};        ///< last composite with the HDR pass
    std::atomic<int> hdr_mode{0};                ///< BB_HDR_TONEMAP
    /// 0 Neutwo, 1 PsychoV30, 2 Vanilla (the game's own look), 3 PsychoV30 on the scene (HDR
    /// 16-bit mode; live, saved).
    std::atomic<int> hdr_tonemapper{0};
    std::atomic<bool> hdr_per_channel{false};    ///< Neutwo per channel (else luminance)
    /// The in-game menu's HDR preset, as RenoDX's: 0 Off (the game's own look), 1..3 the
    /// player's own settings, each kept in bbport.ini (hdr_preset1..3); the sliders change the one
    /// selected. Saved.
    std::atomic<int> hdr_preset{1};
    /// The in-game menu's HDR sliders shown, as RenoDX's Settings Mode: 0 Simple,
    /// 1 Intermediate, 2 Advanced (bbport's own sliders and the test tools). Saved.
    std::atomic<int> hdr_settings_mode{0};
    /// Silence the game while its window does not have focus (bbgpu_audio_muted).
    std::atomic<bool> mute_unfocused{false};
    /// Keyboard and mouse button prompts: with the device in use (auto), or always one kind.
    std::atomic<int> button_prompts{PromptsAuto};
    // FSR 4 checks (menu): the provider's auto exposure, the jitter sign it is given.
    std::atomic<bool> fsr4_auto_exposure{true};
    std::atomic<bool> fsr4_invert_jitter{false};
    std::atomic<int> active_render_width{1920}, active_render_height{1080};
    /// Applied at start (patches.py); the menu shows when a restart is needed.
    std::atomic<bool> effects[EffectCount]{};
    std::atomic<int> model_lod{0}; ///< -2 highest .. 2 lowest, 0 the game's
    std::atomic<int> output_res{OutputDefault}; ///< index into OutputWidths
    /// Ultrawide outputs: the HUD's corner groups, 0 centred in 16:9 .. 100 at the screen edges
    /// (hud_layout.py at start).
    std::atomic<int> hud_position{100};
    /// Vertical field of view in degrees, 43 the game's (run.py, fov.py at start).
    std::atomic<int> fov{FovDefault};
    /// Live resolution and preset changes (run.sh): 0 off by default (startup patch, fastest
    /// on the Steam Deck and older GPUs), -1 auto (strong discrete GPUs), 1 on. On restart.
    std::atomic<int> live_resolution{0};
    /// Why FSR 4 cannot run (assets, device features), or null. Set by the renderer.
    std::atomic<const char*> fsr4_problem{nullptr};
    std::atomic<bool> fsr4_supported{false}, fsr411_supported{false};
    /// DLSS (bbport_dlss.dll, NVIDIA RTX) is ready, or why not (null before the device exists).
    std::atomic<bool> dlss_supported{false};
    std::atomic<const char*> dlss_problem{nullptr};
    /// Frame generation and the frames per game frame (2x with FSR 3.1, up to 4x with DLSS on
    /// RTX 50). dlssg_max_multiplier: 1 without DLSS Frame Generation.
    std::atomic<int> frame_gen{FrameGenOff};
    std::atomic<int> frame_gen_multiplier{2};
    /// At most the frames the display shows between two of the game's (an uncapped game then
    /// gets fewer generated frames); off: the multiplier as chosen.
    std::atomic<bool> frame_gen_fit_display{false};
    std::atomic<int> dlssg_max_multiplier{1};
    std::atomic<const char*> frame_gen_problem{nullptr};
    /// Generated frames per game frame the display has room for now (0: paused), and its
    /// refresh rate (the presenter's measure, for the menu).
    std::atomic<int> frame_gen_now{0};
    std::atomic<int> frame_gen_display_hz{0};
    /// The game's own frame rate (presenter, game frames only), shown beside the total when
    /// frame generation adds frames.
    std::atomic<float> game_fps{0.0f};
    /// The game's frame rate cap frame generation needs (VideoOut applies it), 0 for none: with
    /// FIFO presentation (V-Sync; AMD has no Mailbox) at most refresh / (generated + 1), or each
    /// extra frame waits for the display and generation pauses.
    std::atomic<int> frame_gen_flip_limit{0};

    /// Startup settings for the explicit BB_RENDER_RES compatibility patch only.
    int startup_preset = NativeAA;
    int startup_upscaler = UpscalerFsr3;
    bool startup_object_motion = true;
    bool startup_effects[EffectCount]{};
    int startup_model_lod = 0;
    int startup_output_res = OutputDefault;
    int startup_hud_position = 100;
    int startup_fov = FovDefault;
    int startup_live_resolution = 0;
};

Values& Get();

/// Reads the file, then the environment overrides. Called once at start.
void Load();
/// Checks the loaded choice before the first frame; unsupported FSR 4 uses FSR 3.1.
void ConfigureUpscalerSupport(bool fsr4, bool fsr411);
/// After device creation: DLSS availability; a DLSS setting falls back to FSR 3.1 without it.
void ConfigureDlssSupport(bool available, const char* problem);
/// Startup-patched scene dimensions cannot change until run.sh prepares a new image.
bool FixedRenderSession();
int RenderPreset();
bool ResolutionNeedsRestart();
/// Writes the file (menu changes). With an HDR preset selected, it also keeps the HDR settings
/// as that preset's.
void Save();

inline constexpr int HdrToneMapperVanilla = 2;
/// PsychoV30 on the scene itself, in the composite (HDR 16-bit mode; elsewhere PsychoV30): the
/// game's mid grey kept, the color grading sliders as PsychoV30's own parameters.
inline constexpr int HdrToneMapperPsychoScene = 3;
inline constexpr int HdrPresetCount = 4; ///< Off and three of the player's
/// Selects an HDR preset: the HDR settings go to the one selected before, and those of the new
/// one (1..3) are loaded; a preset never saved starts from the defaults. 0 (Off) keeps them.
void SelectHdrPreset(int preset);
/// The HDR settings back to bbport's defaults (the menu's Reset all).
void ResetHdr();
/// The display's peak luminance Windows reports (vk_swapchain, HDR output; 0 when unknown). It
/// becomes the default Peak Brightness, and the setting itself while bbport.ini has none.
void SetDisplayPeak(float nits);
/// The display's peak, 0 when unknown.
float DisplayPeak();
/// The HDR defaults, as RenoDX's: Peak Brightness the display's peak (1000 when unknown), Game
/// Brightness the BT.2100 reference white for it (203 nits at 1000), at most 203.
float DefaultPeakNits();
float DefaultGameNits();

/// Render resolution divisor of a preset (1.0 native, 1.5 quality, ...).
float PresetScale(int preset);
const char* PresetName(int preset);
const char* UpscalerName(int upscaler);
const char* FrameGenName(int frame_gen);

} // namespace BbSettings
