#pragma once

// Text views of engine state: window title, one-line stats, multi-line report.
// (The ImGui overlay replaces the title display later; these stay for the terminal.)
#include <nitid/nitid.hpp>

#include <string>

namespace nitid_app {

std::string window_title(const nitid::SourceInfo& src, const nitid::Settings& s, const nitid::Stats& st, bool paused);
std::string stats_line(const nitid::Settings& s, const nitid::Stats& st);
std::string stats_report(const nitid::SourceInfo& src, const nitid::Settings& s, const nitid::Stats& st);

} // namespace nitid_app
