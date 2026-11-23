# nitid: the GUI player (SDL3 window + Dear ImGui overlay, added in Phase 1).
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

add_executable(nitid-gui
  src/main.cpp
)

set_target_properties(nitid-gui PROPERTIES
  OUTPUT_NAME nitid                                  # installed binary: nitid
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_compile_features(nitid-gui PRIVATE cxx_std_23)
target_link_libraries(nitid-gui PRIVATE nitid::nitid)

install(TARGETS nitid-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
