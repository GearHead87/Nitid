#pragma once

#include <nitid/export.hpp>

#include <stdexcept>
#include <string>

namespace nitid {

// Thrown by the public API when something fails (no GPU, file can't be opened, effect can't load...).
// The message is meant to be shown to the user as-is.
class NITID_EXPORT Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

} // namespace nitid
