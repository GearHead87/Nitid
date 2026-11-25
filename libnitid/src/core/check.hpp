#pragma once

// Error helpers: turn failed API calls into nitid::Error with file/line context.
#include <nitid/error.hpp>

#include <format>
#include <string>

namespace nitid {

[[noreturn]] inline void fail(const std::string& msg) { throw Error(msg); }

template <class... A> [[noreturn]] void failf(std::format_string<A...> f, A&&... a)
{
    throw Error(std::format(f, std::forward<A>(a)...));
}

} // namespace nitid
