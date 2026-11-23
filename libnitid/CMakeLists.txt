# libnitid: the Nitid engine library.
#
# Has no UI dependencies (no SDL, no ImGui). Apps drive it through its public
# API in include/nitid/. Can be built on its own and installed as a CMake
# package, which apps consume with: find_package(libnitid CONFIG REQUIRED)
# and target_link_libraries(<app> PRIVATE nitid::nitid).

cmake_minimum_required(VERSION 3.25)

project(libnitid VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

option(BUILD_SHARED_LIBS "Build libnitid as a shared library" ON)

add_library(libnitid
  src/version.cpp
)
add_library(nitid::nitid ALIAS libnitid)

set_target_properties(libnitid PROPERTIES
  OUTPUT_NAME nitid                 # -> libnitid.so
  EXPORT_NAME nitid                 # -> imported target nitid::nitid
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden      # only NITID_EXPORT symbols are public
  VISIBILITY_INLINES_HIDDEN ON
)

target_compile_features(libnitid PUBLIC cxx_std_23)

target_compile_definitions(libnitid PRIVATE
  NITID_VERSION_STRING="${PROJECT_VERSION}"
)

generate_export_header(libnitid
  BASE_NAME NITID
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/nitid/export.hpp
)

target_include_directories(libnitid PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# --- install + CMake package ---------------------------------------------------

set(LIBNITID_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/libnitid)

install(TARGETS libnitid
  EXPORT libnitidTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ ${CMAKE_CURRENT_BINARY_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT libnitidTargets
  NAMESPACE nitid::
  DESTINATION ${LIBNITID_CMAKE_DIR}
)

configure_package_config_file(
  cmake/libnitidConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfig.cmake
  INSTALL_DESTINATION ${LIBNITID_CMAKE_DIR}
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfigVersion.cmake
  COMPATIBILITY SameMinorVersion   # 0.x: minor bumps may break the API
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfigVersion.cmake
  DESTINATION ${LIBNITID_CMAKE_DIR}
)
