#pragma once

// The player's on-screen controls (Dear ImGui): a control bar at the bottom (play / pause, position, seek bar,
// volume, panel buttons), a settings panel (upscaling, frame generation, sound), a stats panel and a help panel.
// The bar hides itself while the video plays and the mouse is still. Drawn between Overlay::begin_frame() and
// end_frame().
#include <nitid/nitid.hpp>

#include <optional>
#include <string>

namespace nitid_app {

// What the UI asks the app to do (things the app owns: files, window, settings).
struct UiActions {
    bool open_file = false;          // show the file dialog
    bool toggle_fullscreen = false;
    bool settings_changed = false;   // apply PlayerUi's copy of the settings
    bool quit = false;
};

struct UiState {
    bool visible = true;         // Tab hides everything
    bool show_settings = false;
    bool show_stats = false;
    bool show_help = false;
};

class PlayerUi {
public:
    // source: nullptr before a file is open. now_s: app clock (for auto-hide). error: last error to show, or empty.
    UiActions draw(nitid::Engine& engine, nitid::Settings& settings, const nitid::SourceInfo* source, bool ended,
                   bool fullscreen, double now_s, const std::string& error);
    void note_mouse_activity(double now_s) { last_activity_s_ = now_s; }
    bool controls_shown() const { return controls_shown_; }  // the app hides the mouse pointer when false
    UiState& state() { return state_; }

private:
    void control_bar(nitid::Engine& e, const nitid::SourceInfo& src, bool ended, bool fullscreen, UiActions& a);
    void settings_panel(nitid::Engine& e, nitid::Settings& s, const nitid::Stats& st, UiActions& a);
    void stats_panel(const nitid::SourceInfo& src, const nitid::Stats& st, const nitid::Settings& s);
    void help_panel();

    UiState state_;
    double last_activity_s_ = 0;
    bool controls_shown_ = true;
    double last_seek_s_ = -1;  // throttles seeking while the seek bar is dragged
};

} // namespace nitid_app
