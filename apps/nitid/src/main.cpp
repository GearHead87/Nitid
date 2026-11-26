// nitid: the GUI player.
//
//   nitid [options] <video file>
//
// Plays the file with AI upscaling and frame generation, with on-screen controls (F1 lists the keys).
#include "app.hpp"

#include <CLI/CLI.hpp>
#include <nitid/version.hpp>

#include <algorithm>
#include <cstdio>
#include <print>

int main(int argc, char** argv)
{
    using namespace nitid;
    nitid_app::AppOptions o;
    std::string upscale = "high", frame_gen = "medium";
    int multiplier = 0;

    CLI::App cli{"Nitid: AI upscaling and frame generation for video (NVIDIA RTX, Linux)", "nitid"};
    cli.set_version_flag("--version", std::string(nitid::version()));
    cli.add_option("file", o.file, "Video file to play (or open one from the window)")->check(CLI::ExistingFile);
    cli.add_option("--upscale", upscale, "Upscaling: VSR (AI) quality low, medium, high, ultra; fsr; nis; off")
        ->check(CLI::IsMember({"off", "low", "medium", "high", "ultra", "fsr", "nis"}))
        ->capture_default_str();
    cli.add_option("--frame-gen", frame_gen, "Frame generation: off, low, medium, high")
        ->check(CLI::IsMember({"off", "low", "medium", "high"}))
        ->capture_default_str();
    cli.add_option("--sharpness", o.settings.sharpness, "FSR / NIS sharpening, 0 to 1")
        ->check(CLI::Range(0.0f, 1.0f))
        ->capture_default_str();
    cli.add_option("--multiplier", multiplier, "Frame gen multiplier: 2, 3, 4, or 0 = one frame per display refresh")
        ->check(CLI::Range(0, 8))
        ->capture_default_str();
    cli.add_flag("--compare", o.settings.compare, "Side by side: original (left) | Nitid (right); X toggles");
    cli.add_option("--compare-split", o.compare_split, "Where the compare line is, 0 to 1")->check(CLI::Range(0.0, 1.0));
    cli.add_flag("!--no-scene-cuts", o.settings.scene_cut_detection, "Turn off scene-cut detection");
    cli.add_flag("--fullscreen", o.fullscreen, "Start fullscreen");
    cli.add_option("--width", o.width, "Window width in pixels (with --height)");
    cli.add_option("--height", o.height, "Window height in pixels (with --width)");
    cli.add_option("--start", o.start_s, "Start at this time in seconds");
    cli.add_flag("!--no-audio", o.open.audio, "Play without sound");
    cli.add_option("--volume", o.volume, "Volume, 0 to 2 (1 = as recorded)")->check(CLI::Range(0.0, 2.0))->capture_default_str();
    cli.add_option("--audio-delay", o.audio_delay_ms, "Move the sound against the picture, in ms (+ = later)");
    cli.add_flag("--loop", o.open.loop, "Play again from the start at the end");
    cli.add_flag("--exit-at-end", o.exit_at_end, "Quit when the video ends (default: stay on the last frame)");
    std::string ui = "auto";
    cli.add_option("--ui", ui, "On-screen controls: on, off, auto (off when --capture-at is used)")
        ->check(CLI::IsMember({"on", "off", "auto"}));
    cli.add_option("--test-panel", o.test_panels, "Tests: open panels at start (settings, stats, help)")
        ->check(CLI::IsMember({"settings", "stats", "help"}));
    cli.add_flag("!--sw-decode", o.open.hw_decode, "Decode in software instead of NVDEC");
    cli.add_option("--source-step", o.open.source_step,
                   "Use only every Nth frame of the file (tests: frame gen rebuilds the dropped ones)")
        ->check(CLI::Range(1, 16));
    cli.add_option("--quit-after", o.quit_after_s, "Close after this many seconds and print a report (for tests)");
    cli.add_flag("--stats", o.print_stats, "Print a stats line every second");
    cli.add_flag("--validation", o.validation, "Enable Vulkan validation layers");
    cli.add_option("--capture-at", o.capture_at, "Save the window image at these playback times in seconds, then quit (tests)")
        ->expected(1, -1);
    std::vector<std::string> test_seeks;
    cli.add_option("--test-seek", test_seeks, "Tests: seek while playing, AT:TO = after AT s of the run, go to TO s")
        ->expected(1, -1);
    std::vector<std::string> test_keys;
    cli.add_option("--test-key", test_keys, "Tests: press a key while playing, AT:KEY = after AT s of the run (SDL key name)")
        ->expected(1, -1);
    std::vector<std::string> test_pauses;
    cli.add_option("--test-pause", test_pauses, "Tests: pause while playing, AT:LEN = after AT s of the run, for LEN s")
        ->expected(1, -1);
    cli.add_option("--capture-prefix", o.capture_prefix, "File name prefix for --capture-at images");
    CLI11_PARSE(cli, argc, argv);

    o.ui = ui == "on" ? 1 : ui == "off" ? 0 : -1;
    parse_upscaling(upscale, o.settings);  // names already checked by CLI11
    parse_frame_gen(frame_gen, o.settings);
    o.settings.frame_gen_multiplier = multiplier;

    std::sort(o.capture_at.begin(), o.capture_at.end());
    for (const auto& t : test_seeks) {
        double at = 0, to = 0;
        if (std::sscanf(t.c_str(), "%lf:%lf", &at, &to) != 2) {
            std::println(stderr, "--test-seek wants AT:TO, got {}", t);
            return 2;
        }
        o.test_seeks.emplace_back(at, to);
    }
    std::sort(o.test_seeks.begin(), o.test_seeks.end());
    for (const auto& t : test_pauses) {
        double at = 0, len = 0;
        if (std::sscanf(t.c_str(), "%lf:%lf", &at, &len) != 2) {
            std::println(stderr, "--test-pause wants AT:LEN, got {}", t);
            return 2;
        }
        o.test_pauses.emplace_back(at, len);
    }
    std::sort(o.test_pauses.begin(), o.test_pauses.end());
    for (const auto& t : test_keys) {
        auto colon = t.find(':');
        if (colon == std::string::npos) {
            std::println(stderr, "--test-key wants AT:KEY, got {}", t);
            return 2;
        }
        o.test_keys.emplace_back(std::stod(t.substr(0, colon)), t.substr(colon + 1));
    }
    std::sort(o.test_keys.begin(), o.test_keys.end());
    try {
        nitid_app::App app(std::move(o));
        return app.run();
    } catch (const nitid::Error& e) {
        std::println(stderr, "nitid: {}", e.what());
        return 1;
    }
}
