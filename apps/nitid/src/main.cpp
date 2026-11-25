// nitid: the GUI player.
//
//   nitid [options] <video file>
//
// Plays the file with NVIDIA VFX AI upscaling and frame generation. Keys are printed at start (F1).
#include "app.hpp"

#include <CLI/CLI.hpp>

#include <map>
#include <print>

int main(int argc, char** argv)
{
    using namespace nitid;
    nitid_app::AppOptions o;
    std::string upscale = "high", frame_gen = "medium";
    int multiplier = 0;

    CLI::App cli{"Nitid: AI upscaling and frame generation for video (NVIDIA RTX, Linux)", "nitid"};
    cli.add_option("file", o.file, "Video file to play")->required()->check(CLI::ExistingFile);
    cli.add_option("--upscale", upscale, "AI upscaling: off, low, medium, high, ultra")
        ->check(CLI::IsMember({"off", "low", "medium", "high", "ultra"}))
        ->capture_default_str();
    cli.add_option("--frame-gen", frame_gen, "Frame generation: off, low, medium, high")
        ->check(CLI::IsMember({"off", "low", "medium", "high"}))
        ->capture_default_str();
    cli.add_option("--multiplier", multiplier, "Frame gen multiplier: 2, 3, 4, or 0 = one frame per display refresh")
        ->check(CLI::Range(0, 8))
        ->capture_default_str();
    cli.add_flag("!--no-scene-cuts", o.settings.scene_cut_detection, "Turn off scene-cut detection");
    cli.add_flag("--fullscreen", o.fullscreen, "Start fullscreen");
    cli.add_option("--width", o.width, "Window width in pixels (with --height)");
    cli.add_option("--height", o.height, "Window height in pixels (with --width)");
    cli.add_flag("--loop", o.open.loop, "Play again from the start at the end");
    cli.add_flag("!--sw-decode", o.open.hw_decode, "Decode in software instead of NVDEC");
    cli.add_option("--quit-after", o.quit_after_s, "Close after this many seconds and print a report (for tests)");
    cli.add_flag("--stats", o.print_stats, "Print a stats line every second");
    cli.add_flag("--validation", o.validation, "Enable Vulkan validation layers");
    cli.add_option("--capture-at", o.capture_at, "Save the window image at these playback times in seconds (tests)")
        ->expected(1, -1);
    cli.add_option("--capture-prefix", o.capture_prefix, "File name prefix for --capture-at images");
    CLI11_PARSE(cli, argc, argv);

    static const std::map<std::string, SuperResQuality> sr = {{"low", SuperResQuality::Low},
                                                              {"medium", SuperResQuality::Medium},
                                                              {"high", SuperResQuality::High},
                                                              {"ultra", SuperResQuality::Ultra}};
    static const std::map<std::string, FrameGenQuality> fg = {
        {"low", FrameGenQuality::Low}, {"medium", FrameGenQuality::Medium}, {"high", FrameGenQuality::High}};
    o.settings.upscaler = upscale == "off" ? Upscaler::Off : Upscaler::VfxSuperRes;
    if (upscale != "off") o.settings.super_res_quality = sr.at(upscale);
    o.settings.frame_gen = frame_gen == "off" ? FrameGen::Off : FrameGen::VfxFrameGen;
    if (frame_gen != "off") o.settings.frame_gen_quality = fg.at(frame_gen);
    o.settings.frame_gen_multiplier = multiplier;

    try {
        nitid_app::App app(std::move(o));
        return app.run();
    } catch (const nitid::Error& e) {
        std::println(stderr, "nitid: {}", e.what());
        return 1;
    }
}
