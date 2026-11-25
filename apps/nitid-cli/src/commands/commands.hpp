#pragma once

// nitid-cli subcommands. Each one registers its options on the CLI11 app and runs when selected.
// Later: export (record the enhanced video), virtual-camera, quality (automated A/B scores).
#include <CLI/CLI.hpp>

#include <functional>

namespace nitid_cli {

using Runner = std::function<int()>;

Runner add_info_command(CLI::App& app);
Runner add_bench_command(CLI::App& app);

} // namespace nitid_cli
