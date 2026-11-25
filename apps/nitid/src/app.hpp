#pragma once

// The GUI player: owns the SDL window and drives the engine once per display refresh.
#include "key_bindings.hpp"

#include <nitid/nitid.hpp>

#include <memory>
#include <string>

struct SDL_Window;

namespace nitid_app {

struct AppOptions {
    std::string file;
    nitid::Settings settings;
    nitid::OpenOptions open;
    bool fullscreen = false;
    int width = 0, height = 0;   // 0 = fit the video into 80% of the screen
    double quit_after_s = 0;     // > 0: close after this long (for tests); prints a final report
    bool print_stats = false;    // one stats line per second on the terminal
    bool validation = false;
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
