// nitid-cli info [file]: what this machine supports, and what Nitid sees in a file.
#include "commands/commands.hpp"

#include <nitid/nitid.hpp>

#include <memory>
#include <print>

namespace nitid_cli {

Runner add_info_command(CLI::App& app)
{
    auto* cmd = app.add_subcommand("info", "Show the GPU, the AI effects available and (optionally) a file's details");
    auto file = std::make_shared<std::string>();
    cmd->add_option("file", *file, "Video file to inspect")->check(CLI::ExistingFile);
    return [cmd, file]() -> int {
        if (!*cmd) return -1;
        auto engine = nitid::Engine::create();
        const auto& c = engine->capabilities();
        std::println("libnitid        {}", nitid::version());
        std::println("GPU             {} (driver {})", c.gpu_name, c.driver_version);
        std::println("VFX runtime     {}", c.vfx_version.empty() ? "not found: " + c.vfx_error : c.vfx_version);
        std::println("AI upscaling    {}", c.super_res ? "yes (VFX Video Super Resolution)" : "no");
        std::println("Frame gen       {}", c.frame_gen ? "yes (VFX Video Frame Generation)" : "no");
        if (c.vfx_version.empty())
            std::println("                AI effects need NVIDIA's VFX runtime: run nitid-setup-vfx once");
        if (!file->empty()) {
            auto s = engine->open_file(*file);
            std::println("File            {}", s.name);
            std::println("  video         {} {}x{} @ {:.3f} fps, {:.1f} s", s.codec, s.width, s.height, s.fps, s.duration_s);
            std::println("  decoder       {}", s.decoder);
        }
        return 0;
    };
}

} // namespace nitid_cli
