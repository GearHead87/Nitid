#include <nitid/version.hpp>

namespace nitid {

std::string_view version() noexcept
{
    return NITID_VERSION_STRING;
}

} // namespace nitid
