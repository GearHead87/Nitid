// nitid-cli: Nitid without a window (benchmarks, quality tests; later export and virtual camera).
//
//   nitid-cli info [file]
//   nitid-cli bench <file> [--upscale high] [--frame-gen medium] [--scale 2] [-o out.mkv] ...
#include "commands/commands.hpp"

#include <nitid/nitid.hpp>

#include <print>

int main(int argc, char** argv)
{
    CLI::App app{"nitid-cli: Nitid's AI video pipeline without a window", "nitid-cli"};
    app.require_subcommand(1);
    app.set_version_flag("--version", std::string(nitid::version()));
    std::vector<nitid_cli::Runner> commands = {nitid_cli::add_info_command(app), nitid_cli::add_bench_command(app)};
    CLI11_PARSE(app, argc, argv);
    try {
        for (auto& run : commands)
            if (int rc = run(); rc >= 0) return rc;
    } catch (const nitid::Error& e) {
        std::println(stderr, "nitid-cli: {}", e.what());
        return 1;
    }
    return 0;
}
