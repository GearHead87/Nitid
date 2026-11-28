# nitid-cli: headless tool (export, benchmarks, virtual camera, tests).
#
# Talks to the engine only through libnitid's public API. Builds inside the
# top-level project, or standalone against an installed libnitid:
#   cmake -S apps/nitid-cli -B build-cli -DCMAKE_PREFIX_PATH=<libnitid prefix>

cmake_minimum_required(VERSION 3.25)

project(nitid-cli VERSION 0.5.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET nitid::nitid)
  find_package(libnitid 0.5 CONFIG REQUIRED)
endif()

find_package(CLI11 REQUIRED CONFIG)

add_executable(nitid-cli
  src/main.cpp                 # subcommand dispatch
  src/commands/info.cpp        # nitid-cli info
  src/commands/bench.cpp       # nitid-cli bench
  src/util/video_writer.cpp    # RGBA -> video file (ffmpeg pipe)
)
target_include_directories(nitid-cli PRIVATE src)
target_compile_options(nitid-cli PRIVATE -Wall -Wextra)

set_target_properties(nitid-cli PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_compile_features(nitid-cli PRIVATE cxx_std_23)
target_link_libraries(nitid-cli PRIVATE nitid::nitid CLI11::CLI11)

install(TARGETS nitid-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
