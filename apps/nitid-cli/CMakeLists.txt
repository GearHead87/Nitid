# nitid-cli: headless tool (export, benchmarks, virtual camera, tests).
#
# Talks to the engine only through libnitid's public API. Builds inside the
# top-level project, or standalone against an installed libnitid:
#   cmake -S apps/nitid-cli -B build-cli -DCMAKE_PREFIX_PATH=<libnitid prefix>

cmake_minimum_required(VERSION 3.25)

project(nitid-cli VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET nitid::nitid)
  find_package(libnitid 0.1 CONFIG REQUIRED)
endif()

add_executable(nitid-cli
  src/main.cpp
)

set_target_properties(nitid-cli PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_compile_features(nitid-cli PRIVATE cxx_std_23)
target_link_libraries(nitid-cli PRIVATE nitid::nitid)

install(TARGETS nitid-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
