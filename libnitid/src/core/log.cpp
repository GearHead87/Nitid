#include "core/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace nitid::log {

Level level() noexcept
{
    static const Level lvl = [] {
        const char* e = std::getenv("NITID_LOG");
        std::string s = e ? e : "";
        if (s == "error") return Level::Error;
        if (s == "warn") return Level::Warn;
        if (s == "debug") return Level::Debug;
        return Level::Info;
    }();
    return lvl;
}

void write(Level lvl, std::string_view msg)
{
    static std::mutex m;
    static constexpr const char* tag[] = {"error", "warn", "info", "debug"};
    std::lock_guard lock(m);
    std::fprintf(stderr, "[nitid %s] %.*s\n", tag[int(lvl)], int(msg.size()), msg.data());
}

} // namespace nitid::log
