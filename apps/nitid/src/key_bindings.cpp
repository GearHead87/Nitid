#include "key_bindings.hpp"

#include <array>

namespace nitid_app {

namespace {

constexpr std::array kBindings = {
    KeyBinding{SDLK_ESCAPE, Action::Quit, "Esc / Q", "quit"},
    KeyBinding{SDLK_Q, Action::Quit, "", ""},
    KeyBinding{SDLK_SPACE, Action::TogglePause, "Space", "pause / play"},
    KeyBinding{SDLK_F11, Action::ToggleFullscreen, "F11 / F", "fullscreen"},
    KeyBinding{SDLK_F, Action::ToggleFullscreen, "", ""},
    KeyBinding{SDLK_V, Action::CycleUpscaler, "V", "upscaler: VSR (AI) / FSR 1 / NIS / off"},
    KeyBinding{SDLK_B, Action::CycleUpscalerQuality, "B", "VSR quality: low / medium / high / ultra"},
    KeyBinding{SDLK_G, Action::ToggleFrameGen, "G", "frame generation on / off"},
    KeyBinding{SDLK_H, Action::CycleFrameGenQuality, "H", "frame gen quality: low / medium / high"},
    KeyBinding{SDLK_0, Action::MultiplierMatchDisplay, "0", "frame gen: one frame per display refresh"},
    KeyBinding{SDLK_2, Action::Multiplier2, "2", "frame gen: 2x"},
    KeyBinding{SDLK_3, Action::Multiplier3, "3", "frame gen: 3x"},
    KeyBinding{SDLK_4, Action::Multiplier4, "4", "frame gen: 4x"},
    KeyBinding{SDLK_C, Action::ToggleSceneCuts, "C", "scene-cut detection on / off"},
    KeyBinding{SDLK_R, Action::ResetStats, "R", "reset stats"},
    KeyBinding{SDLK_S, Action::PrintStats, "S", "print stats to the terminal"},
    KeyBinding{SDLK_F1, Action::ShowHelp, "F1", "print this help"},
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
