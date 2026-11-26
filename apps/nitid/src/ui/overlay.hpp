#pragma once

// Dear ImGui on top of the video: SDL3 backend for input, Vulkan backend drawing through the engine's overlay hook
// (dynamic rendering on the window image, after the video). Everything ImGui-specific lives here; the panels are in
// player_ui.*.
#include <nitid/engine.hpp>

union SDL_Event;
struct SDL_Window;

namespace nitid_app {

class Overlay {
public:
    Overlay(SDL_Window* window, nitid::Engine& engine);
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void process_event(const SDL_Event& ev);
    bool wants_keyboard() const;  // a text field / widget has focus: don't treat keys as shortcuts
    bool wants_mouse() const;     // the mouse is over a panel

    void begin_frame();  // before building the UI
    void end_frame();    // after building it; the engine draws it in the next render_frame()

private:
    nitid::Engine& engine_;
};

} // namespace nitid_app
