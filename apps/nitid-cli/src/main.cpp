#include <nitid/version.hpp>

#include <print>

int main()
{
    // Placeholder until headless commands land.
    std::println("nitid-cli using libnitid {}", nitid::version());
    return 0;
}
