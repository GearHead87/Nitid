#pragma once

// The GUI player: owns the SDL window, the on-screen controls (Dear ImGui) and drives the engine once per display
// refresh.
#include "key_bindings.hpp"
#include "ui/player_ui.hpp"

#include <nitid/nitid.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;

namespace nitid_app {

class Overlay;

struct AppOptions {
    std::string file;            // empty: start with the "open a file" screen
    std::string pipewire;        // live input from a PipeWire node (id, name or serial) instead of a file
    bool capture_screen = false; // live input: screen capture through the desktop portal
    nitid::Settings settings;
    nitid::OpenOptions open;
    bool fullscreen = false;
    int width = 0, height = 0;   // 0 = fit the video into 80% of the screen
    double start_s = 0;          // start playing at this time
    double volume = 1.0;         // linear gain
    double audio_delay_ms = 0;   // + = sound later
    double compare_split = 0.5;  // compare mode: where the line is (0..1)
    bool exit_at_end = false;    // quit when the video ends (default: stay on the last frame)
    double quit_after_s = 0;     // > 0: close after this long (for tests); prints a final report
    bool print_stats = false;    // one stats line per second on the terminal
    bool validation = false;
    // On-screen controls: shown unless --capture-at is used (test images show the video only) or forced.
    int ui = -1;                 // -1 auto, 0 off, 1 on
    // Test hook: save the window image at these playback times (seconds) to "<prefix>_<time>s.ppm".
    std::vector<double> capture_at;
    std::string capture_prefix = "nitid_capture";
    // Test hook: seek while playing. Each entry: {seconds since start of the run, target position}.
    std::vector<std::pair<double, double>> test_seeks;
    // Test hook: pause while playing. Each entry: {seconds since start of the run, pause length in seconds}.
    std::vector<std::pair<double, double>> test_pauses;
    // Test hook: press keys while playing. Each entry: {seconds since start of the run, SDL key name, e.g. "H"}.
    std::vector<std::pair<double, std::string>> test_keys;
    // Test hook: open these panels at start ("settings", "stats", "help").
    std::vector<std::string> test_panels;
};

class App {
public:
    explicit App(AppOptions options);
    ~App();
    int run();

private:
    void handle(Action a);
    bool open(const std::string& path);
    void open_live(const std::string& pipewire_target);  // empty: screen capture through the portal
    void show_open_dialog();
    void update_title();
    void sync_display();
    void apply_settings();

    AppOptions opts_;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<nitid::Engine> engine_;
    std::unique_ptr<Overlay> overlay_;
    PlayerUi ui_;
    nitid::SourceInfo source_;
    bool has_source_ = false;
    bool ended_ = false;
    bool shaped_ = false;        // window already fitted to a video
    std::string error_;          // shown on screen until the next file opens
    nitid::Settings settings_;
    nitid::FrameGen last_frame_gen_ = nitid::FrameGen::VfxFrameGen;  // G turns this back on
    uint32_t dialog_event_ = 0;  // SDL user event carrying a file chosen in the dialog
    bool running_ = true;
    bool fullscreen_ = false;
};

} // namespace nitid_app
