# Nitid: top-level build.
#
# Builds the engine library and both apps in one go. Each component is also a
# standalone CMake project and can be configured and built on its own:
#
#   libnitid/        engine library  (installs a CMake package: find_package(libnitid))
#   apps/nitid/      GUI player      (links libnitid)
#   apps/nitid-cli/  headless tool   (links libnitid)
#
# Inside this combined build the apps link the in-tree engine target directly;
# when built standalone they locate an installed libnitid instead.

cmake_minimum_required(VERSION 3.25)

project(nitid_superbuild VERSION 0.3.0 LANGUAGES CXX)

option(NITID_BUILD_GUI "Build the nitid GUI player" ON)
option(NITID_BUILD_CLI "Build the nitid-cli headless tool" ON)
option(NITID_BUILD_TESTS "Build libnitid unit tests (ctest)" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NITID_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(libnitid)

if(NITID_BUILD_GUI)
  add_subdirectory(apps/nitid)
endif()

if(NITID_BUILD_CLI)
  add_subdirectory(apps/nitid-cli)
endif()
