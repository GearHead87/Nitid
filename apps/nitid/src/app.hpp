#pragma once

// The GUI player: owns the SDL window and drives the engine once per display refresh.
#include "key_bindings.hpp"

#include <nitid/nitid.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;

namespace nitid_app {

struct AppOptions {
    std::string file;
    nitid::Settings settings;
    nitid::OpenOptions open;
    bool fullscreen = false;
    int width = 0, height = 0;   // 0 = fit the video into 80% of the screen
    double start_s = 0;          // start playing at this time
    double volume = 1.0;         // linear gain
    double audio_delay_ms = 0;   // + = sound later
    double quit_after_s = 0;     // > 0: close after this long (for tests); prints a final report
    bool print_stats = false;    // one stats line per second on the terminal
    bool validation = false;
    // Test hook: save the window image at these playback times (seconds) to "<prefix>_<time>s.ppm".
    std::vector<double> capture_at;
    std::string capture_prefix = "nitid_capture";
    // Test hook: seek while playing. Each entry: {seconds since start of the run, target position}.
    std::vector<std::pair<double, double>> test_seeks;
    // Test hook: pause while playing. Each entry: {seconds since start of the run, pause length in seconds}.
    std::vector<std::pair<double, double>> test_pauses;
};

class App {
public:
    explicit App(AppOptions options);
    ~App();
    int run();

private:
    void handle(Action a);
    void update_title();
    void sync_display();

    AppOptions opts_;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<nitid::Engine> engine_;
    nitid::SourceInfo source_;
    nitid::Settings settings_;
    bool running_ = true;
    bool fullscreen_ = false;
};

} // namespace nitid_app
