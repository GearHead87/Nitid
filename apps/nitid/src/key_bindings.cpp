#include "key_bindings.hpp"

#include <array>

namespace nitid_app {

namespace {

constexpr std::array kBindings = {
    KeyBinding{SDLK_ESCAPE, Action::Quit, "Esc / Q", "quit"},
    KeyBinding{SDLK_Q, Action::Quit, "", ""},
    KeyBinding{SDLK_SPACE, Action::TogglePause, "Space", "pause / play"},
    KeyBinding{SDLK_LEFT, Action::SeekBack, "Left / Right", "seek -5 s / +5 s"},
    KeyBinding{SDLK_RIGHT, Action::SeekForward, "", ""},
    KeyBinding{SDLK_PAGEDOWN, Action::SeekBackLong, "PgDn / PgUp", "seek -60 s / +60 s"},
    KeyBinding{SDLK_PAGEUP, Action::SeekForwardLong, "", ""},
    KeyBinding{SDLK_HOME, Action::SeekStart, "Home", "back to the start"},
    KeyBinding{SDLK_UP, Action::VolumeUp, "Up / Down", "volume"},
    KeyBinding{SDLK_DOWN, Action::VolumeDown, "", ""},
    KeyBinding{SDLK_M, Action::ToggleMute, "M", "mute"},
    KeyBinding{SDLK_COMMA, Action::AudioEarlier, ", / .", "sound 10 ms earlier / later (A/V offset)"},
    KeyBinding{SDLK_PERIOD, Action::AudioLater, "", ""},
    KeyBinding{SDLK_F11, Action::ToggleFullscreen, "F11 / F", "fullscreen"},
    KeyBinding{SDLK_F, Action::ToggleFullscreen, "", ""},
    KeyBinding{SDLK_V, Action::CycleUpscaler, "V", "upscaler: VSR (AI) / DLSS (AI) / FSR 1 / NIS / off"},
    KeyBinding{SDLK_B, Action::CycleUpscalerQuality, "B", "VSR quality: low / medium / high / ultra"},
    KeyBinding{SDLK_G, Action::ToggleFrameGen, "G", "frame generation on / off"},
    KeyBinding{SDLK_H, Action::CycleFrameGenQuality, "H", "frame gen quality: low / medium / high (DLSS: full / lite)"},
    KeyBinding{SDLK_D, Action::SwitchFrameGenKind, "D", "frame generation: VFX / DLSS"},
    KeyBinding{SDLK_0, Action::MultiplierMatchDisplay, "0", "frame gen: one frame per display refresh"},
    KeyBinding{SDLK_2, Action::Multiplier2, "2", "frame gen: 2x"},
    KeyBinding{SDLK_3, Action::Multiplier3, "3", "frame gen: 3x"},
    KeyBinding{SDLK_4, Action::Multiplier4, "4", "frame gen: 4x"},
    KeyBinding{SDLK_6, Action::Multiplier6, "6", "frame gen: 6x (DLSS, RTX 50)"},
    KeyBinding{SDLK_C, Action::ToggleSceneCuts, "C", "scene-cut detection on / off"},
    KeyBinding{SDLK_X, Action::ToggleCompare, "X", "compare: original | Nitid side by side (drag to move the line)"},
    KeyBinding{SDLK_L, Action::ToggleLiveMode, "L", "screen capture: low latency / smooth (frame generation)"},
    KeyBinding{SDLK_R, Action::ResetStats, "R", "reset stats"},
    KeyBinding{SDLK_S, Action::PrintStats, "S", "print stats to the terminal"},
    KeyBinding{SDLK_O, Action::OpenFile, "O", "open a file"},
    KeyBinding{SDLK_P, Action::ToggleSettingsPanel, "P", "settings panel"},
    KeyBinding{SDLK_I, Action::ToggleStatsPanel, "I", "stats panel"},
    KeyBinding{SDLK_TAB, Action::ToggleUi, "Tab", "hide / show all on-screen controls"},
    KeyBinding{SDLK_F1, Action::ShowHelp, "F1", "this list of keys"},
};

} // namespace

std::span<const KeyBinding> key_bindings() { return kBindings; }

std::optional<Action> action_for_key(SDL_Keycode key)
{
    for (const auto& b : kBindings)
        if (b.key == key) return b.action;
    return std::nullopt;
}

} // namespace nitid_app
