// nitid-cli bench <file>: run the whole pipeline without a window, as fast as possible, and report
// per-stage times. Optionally saves the output video (for quality checks against a reference).
#include "commands/commands.hpp"
#include "util/video_writer.hpp"

#include <nitid/nitid.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <print>
#include <vector>

namespace nitid_cli {

namespace {

struct BenchOptions {
    std::string file;
    std::string upscale = "high";
    std::string frame_gen = "off";
    int multiplier = 2;
    std::string size;      // WxH output box; empty = source size x scale
    double scale = 1.0;
    int source_step = 1;
    long frames = 0;       // stop after this many output frames (0 = whole file)
    std::string output;
    bool lossless = false;
    bool scene_cuts = true;
    bool hw_decode = true;
    bool verbose = false;
    float sharpness = 0.5f;
    double start = 0;
    std::string cleanup = "off";
};

int run(const BenchOptions& o)
{
    if (o.verbose) setenv("NITID_LOG", "debug", 1);
    nitid::Settings s;
    nitid::parse_upscaling(o.upscale, s);
    nitid::parse_frame_gen(o.frame_gen, s);
    s.frame_gen_multiplier = o.multiplier;
    s.scene_cut_detection = o.scene_cuts;
    s.sharpness = o.sharpness;
    nitid::parse_cleanup(o.cleanup, s);

    auto engine = nitid::Engine::create();
    engine->set_settings(s);
    nitid::OpenOptions oo;
    oo.source_step = o.source_step;
    oo.hw_decode = o.hw_decode;
    auto src = engine->open_file(o.file, oo);

    uint32_t bw = uint32_t(src.width * o.scale + 0.5), bh = uint32_t(src.height * o.scale + 0.5);
    if (!o.size.empty() && std::sscanf(o.size.c_str(), "%ux%u", &bw, &bh) != 2) {
        std::println(stderr, "--size must look like 1920x1080");
        return 2;
    }
    engine->set_headless_output(bw, bh);
    if (o.start > 0) engine->seek(o.start);
    const uint32_t ow = engine->headless_width(), oh = engine->headless_height();
    const double out_fps = engine->headless_fps();

    std::println("Input     {} ({} {}x{} @ {:.3f} fps, decoder {}{})", src.name, src.codec, src.width, src.height, src.fps,
                 src.decoder, o.source_step > 1 ? std::format(", every {} frames", o.source_step) : "");
    std::println("Output    {}x{} @ {:.3f} fps (box {}x{})", ow, oh, out_fps, bw, bh);

    std::optional<VideoWriter> writer;
    std::vector<uint8_t> rgba;
    if (!o.output.empty()) {
        writer.emplace(o.output, ow, oh, out_fps, o.lossless);
        rgba.resize(size_t(ow) * oh * 4);
    }

    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    std::optional<clk::time_point> t_first;
    long n = 0;
    while ((o.frames <= 0 || n < o.frames) && engine->render_headless(rgba)) {
        if (writer && !writer->write(rgba)) {
            std::println(stderr, "writing {} failed", o.output);
            return 1;
        }
        if (!t_first) t_first = clk::now();  // after model loading and the first frame
        ++n;
    }
    auto t1 = clk::now();
    if (writer && !writer->close()) {
        std::println(stderr, "ffmpeg failed writing {}", o.output);
        return 1;
    }
    double total = std::chrono::duration<double>(t1 - t0).count();
    double steady = t_first ? std::chrono::duration<double>(t1 - *t_first).count() : 0;
    auto st = engine->stats();

    std::println("");
    std::println("| Item | Value |");
    std::println("|---|---|");
    std::println("| Upscaling | {}{} |", st.upscaler_active ? nitid::upscaling_name(s) : std::format("off ({})", st.upscaler_note),
                 st.upscaler_mode.empty() ? std::string() : " (" + st.upscaler_mode + ")");
    std::println("| Clean-up | {} |", st.cleanup_active ? std::format("{} {}", nitid::to_string(s.cleanup), nitid::to_string(s.cleanup_strength))
                                                        : s.cleanup == nitid::Cleanup::Off ? std::string("off") : "not running (" + st.cleanup_note + ")");
    std::println("| Frame generation | {} |",
                 st.frame_gen_active ? std::format("{} x{}", nitid::to_string(s.frame_gen_quality), o.multiplier > 0 ? o.multiplier : 2)
                                     : std::format("off ({})", st.frame_gen_note));
    std::println("| Output frames | {} ({} generated, {} real) |", n, st.generated_frames, st.source_frames);
    std::println("| Scene cuts detected | {} |", st.scene_cuts);
    std::println("| Time | {:.2f} s total, {:.2f} s after the first frame |", total, steady);
    std::println("| Throughput | {:.1f} output fps ({:.2f}x real time) |", n > 1 && steady > 0 ? (n - 1) / steady : 0,
                 n > 1 && steady > 0 ? ((n - 1) / steady) / out_fps : 0);
    std::println("| Decode + convert (per source frame, decoder thread) | {:.2f} ms |", st.avg.decode_ms);
    std::println("| Upload (per source frame) | {:.2f} ms |", st.avg.upload_ms);
    std::println("| Upscale / clean-up (per source frame) | {:.2f} ms |", st.avg.upscale_ms);
    std::println("| Frame gen (per generated frame) | {:.2f} ms |", st.avg.frame_gen_ms);
    std::println("| Copy to CPU (per output frame) | {} |", writer ? std::format("{:.2f} ms", st.avg.output_ms) : "not done (no --output)");
    if (writer) std::println("| Saved | {} |", o.output);
    return 0;
}

} // namespace

Runner add_bench_command(CLI::App& app)
{
    auto o = std::make_shared<BenchOptions>();
    auto* cmd = app.add_subcommand("bench", "Run the pipeline on a file without a window; report speed, optionally save the result");
    cmd->add_option("file", o->file, "Video file")->required()->check(CLI::ExistingFile);
    cmd->add_option("--upscale", o->upscale,
                    "Upscaling: VSR (AI) quality bicubic, low, medium, high, ultra; dlss (or dlss-j/k/l/m: DLSS preset); fsr; nis; off")
        ->check(CLI::IsMember({"off", "bicubic", "low", "medium", "high", "ultra", "dlss", "dlss-j", "dlss-k", "dlss-l", "dlss-m", "fsr", "nis"}))
        ->capture_default_str();
    cmd->add_option("--frame-gen", o->frame_gen, "Frame generation: off, low, medium, high")
        ->check(CLI::IsMember({"off", "low", "medium", "high"}))
        ->capture_default_str();
    cmd->add_option("--cleanup", o->cleanup, "Same-size VFX clean-up when not upscaling: off, denoise[-low|-medium|-high|-ultra], deblur[-...]")
        ->check([](const std::string& v) { nitid::Settings t; return nitid::parse_cleanup(v, t) ? std::string() : "unknown clean-up " + v; })
        ->capture_default_str();
    cmd->add_option("--sharpness", o->sharpness, "FSR / NIS sharpening, 0 to 1")->check(CLI::Range(0.0f, 1.0f))->capture_default_str();
    cmd->add_option("--multiplier", o->multiplier, "Frame gen multiplier (2-8)")->check(CLI::Range(2, 8))->capture_default_str();
    cmd->add_option("--size", o->size, "Output box WxH (upscaling fits the video into it)");
    cmd->add_option("--scale", o->scale, "Output box = source size x this (when --size is not given)")->capture_default_str();
    cmd->add_option("--source-step", o->source_step, "Use only every Nth frame of the file")->check(CLI::Range(1, 16));
    cmd->add_option("--start", o->start, "Start at this time in seconds");
    cmd->add_option("--frames", o->frames, "Stop after this many output frames");
    cmd->add_option("--output,-o", o->output, "Save the output video (H.264 CRF 12, or FFV1 with --lossless)");
    cmd->add_flag("--lossless", o->lossless, "Save with FFV1 (exact, large)");
    cmd->add_flag("!--no-scene-cuts", o->scene_cuts, "Turn off scene-cut detection");
    cmd->add_flag("!--sw-decode", o->hw_decode, "Decode in software instead of NVDEC");
    cmd->add_flag("--verbose,-v", o->verbose, "Debug log (shows each detected scene cut)");
    return [cmd, o]() -> int { return *cmd ? run(*o) : -1; };
}

} // namespace nitid_cli
