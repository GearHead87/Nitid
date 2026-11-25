# nitid: the GUI player (SDL3 window; Dear ImGui overlay comes later).
#
# Talks to the engine only through libnitid's public API. Builds inside the
# top-level project, or standalone against an installed libnitid:
#   cmake -S apps/nitid -B build-gui -DCMAKE_PREFIX_PATH=<libnitid prefix>

cmake_minimum_required(VERSION 3.25)

project(nitid VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET nitid::nitid)
  find_package(libnitid 0.1 CONFIG REQUIRED)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(CLI11 REQUIRED CONFIG)

add_executable(nitid-gui
  src/main.cpp            # command line options
  src/app.cpp             # window + render loop
  src/key_bindings.cpp    # keyboard shortcuts
  src/ui/stats_text.cpp   # title / terminal stats (ImGui overlay later lives in src/ui/)
)
target_include_directories(nitid-gui PRIVATE src)
target_compile_options(nitid-gui PRIVATE -Wall -Wextra)

set_target_properties(nitid-gui PROPERTIES
  OUTPUT_NAME nitid                                  # installed binary: nitid
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_compile_features(nitid-gui PRIVATE cxx_std_23)
target_link_libraries(nitid-gui PRIVATE nitid::nitid SDL3::SDL3 CLI11::CLI11)

install(TARGETS nitid-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
