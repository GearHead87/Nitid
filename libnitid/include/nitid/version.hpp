#pragma once

#include <nitid/export.hpp>

#include <string_view>

namespace nitid {

// Version of the libnitid that is actually loaded at runtime (may differ from
// the headers an app was compiled against when libnitid is packaged separately).
NITID_EXPORT std::string_view version() noexcept;

} // namespace nitid
