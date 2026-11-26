# nitid: the GUI player (SDL3 window, Dear ImGui on-screen controls).
#
# Talks to the engine only through libnitid's public API. Builds inside the
# top-level project, or standalone against an installed libnitid:
#   cmake -S apps/nitid -B build-gui -DCMAKE_PREFIX_PATH=<libnitid prefix>

cmake_minimum_required(VERSION 3.25)

project(nitid VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET nitid::nitid)
  find_package(libnitid 0.3 CONFIG REQUIRED)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)   # ImGui's Vulkan backend
find_package(CLI11 REQUIRED CONFIG)

# Dear ImGui (MIT): pinned release, downloaded at configure time and compiled in. Offline builds:
#   -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui-1.93.0
include(FetchContent)
set(NITID_IMGUI_VERSION 1.93.0)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v${NITID_IMGUI_VERSION}.tar.gz
  URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)  # no CMakeLists.txt in imgui: only downloads
add_library(nitid_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(nitid_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(nitid_imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
set_target_properties(nitid_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(nitid-gui
  src/main.cpp            # command line options
  src/app.cpp             # window + render loop
  src/key_bindings.cpp    # keyboard shortcuts
  src/ui/stats_text.cpp   # title / terminal stats
  src/ui/overlay.cpp      # Dear ImGui setup, drawn through the engine's overlay hook
  src/ui/player_ui.cpp    # control bar, settings / stats / help panels
)
target_include_directories(nitid-gui PRIVATE src)
target_compile_options(nitid-gui PRIVATE -Wall -Wextra)

set_target_properties(nitid-gui PROPERTIES
  OUTPUT_NAME nitid                                  # installed binary: nitid
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_compile_features(nitid-gui PRIVATE cxx_std_23)
target_link_libraries(nitid-gui PRIVATE nitid::nitid SDL3::SDL3 CLI11::CLI11 nitid_imgui)

install(TARGETS nitid-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${imgui_SOURCE_DIR}/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/nitid/licenses/imgui)
