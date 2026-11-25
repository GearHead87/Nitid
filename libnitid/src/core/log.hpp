#pragma once

// Minimal logger: stderr, level from $NITID_LOG (error, warn, info, debug). Default: info.
#include <format>
#include <string_view>

namespace nitid::log {

enum class Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

Level level() noexcept;
void write(Level lvl, std::string_view msg);

template <class... A> void error(std::format_string<A...> f, A&&... a) { write(Level::Error, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::format_string<A...> f, A&&... a)
{
    if (level() >= Level::Warn) write(Level::Warn, std::format(f, std::forward<A>(a)...));
}
template <class... A> void info(std::format_string<A...> f, A&&... a)
{
    if (level() >= Level::Info) write(Level::Info, std::format(f, std::forward<A>(a)...));
}
template <class... A> void debug(std::format_string<A...> f, A&&... a)
{
    if (level() >= Level::Debug) write(Level::Debug, std::format(f, std::forward<A>(a)...));
}

} // namespace nitid::log
