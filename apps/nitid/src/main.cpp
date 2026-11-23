#include <nitid/version.hpp>

#include <print>

int main()
{
    // Placeholder until the SDL3 + ImGui player lands in Phase 1.
    std::println("nitid (GUI) using libnitid {}", nitid::version());
    return 0;
}
