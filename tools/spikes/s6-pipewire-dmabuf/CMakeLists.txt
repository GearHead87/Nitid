find_package(PkgConfig REQUIRED)
pkg_check_modules(S6DEPS REQUIRED IMPORTED_TARGET libpipewire-0.3 libportal glib-2.0 gio-2.0)
add_executable(s6-pipewire-dmabuf main.cpp)
target_link_libraries(s6-pipewire-dmabuf PRIVATE spike_common PkgConfig::S6DEPS)
# SPA's pod-builder macros use C99 compound literals (a GCC extension in C++).
target_compile_options(s6-pipewire-dmabuf PRIVATE -Wno-pedantic -Wno-missing-field-initializers)
