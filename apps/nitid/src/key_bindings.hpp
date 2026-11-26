#pragma once

// Keyboard shortcuts -> player actions. One table, so the help text and the handling never disagree.
// (Gamepad and configurable hotkeys come in Phase 5.)
#include <SDL3/SDL_keycode.h>

#include <optional>
#include <span>
#include <string_view>

namespace nitid_app {

enum class Action {
    Quit,
    TogglePause,
    ToggleFullscreen,
    CycleUpscaler,
    CycleUpscalerQuality,
    ToggleFrameGen,
    CycleFrameGenQuality,
    MultiplierMatchDisplay,
    Multiplier2,
    Multiplier3,
    Multiplier4,
    ToggleSceneCuts,
    ResetStats,
    PrintStats,
    ShowHelp,
};

struct KeyBinding {
    SDL_Keycode key;
    Action action;
    std::string_view keys;  // shown in help
    std::string_view help;
};

std::span<const KeyBinding> key_bindings();
std::optional<Action> action_for_key(SDL_Keycode key);

} // namespace nitid_app
