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
option(NITID_BUILD_TESTS "Build libnitid unit tests" OFF)

# --- dependencies ---------------------------------------------------------------
#
# Vulkan (public: the window API uses Vulkan handles), CUDA driver API, FFmpeg, and the
# NVIDIA VFX SDK C headers. The VFX *runtime* is loaded at run time (dlopen), so a
# machine without it still runs Nitid, just without the AI effects.

get_filename_component(NITID_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(Vulkan REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

set(NITID_VFX_INCLUDE_DIR "${NITID_REPO_ROOT}/third_party/Maxine-VFX-SDK/nvvfx/include"
    CACHE PATH "NVIDIA VFX SDK C headers (nvVideoEffects.h); scripts/fetch-sdks.sh downloads them")
set(NITID_VFX_RUNTIME_DIR "${NITID_REPO_ROOT}/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs"
    CACHE PATH "Developer builds: extra folder searched last for the VFX runtime (libVideoFX.so). Release builds
    set it empty; users install the runtime with nitid-setup-vfx")
if(NOT EXISTS "${NITID_VFX_INCLUDE_DIR}/nvVideoEffects.h")
  message(FATAL_ERROR "VFX SDK headers not found in ${NITID_VFX_INCLUDE_DIR}. Run scripts/fetch-sdks.sh "
                      "or set -DNITID_VFX_INCLUDE_DIR=...")
endif()
if(NITID_VFX_RUNTIME_DIR AND NOT EXISTS "${NITID_VFX_RUNTIME_DIR}/libVideoFX.so")
  message(WARNING "VFX runtime not found in ${NITID_VFX_RUNTIME_DIR} (tools/spikes/vfx-env/setup.sh "
                  "installs it). Nitid will build, but needs NITID_VFX_DIR at run time for AI effects.")
endif()

# --- CUDA kernels (PTX, embedded) ----------------------------------------------------
#
# Nitid's own kernels (GPU colour conversion) are compiled to PTX by nvcc and embedded as text; the driver
# compiles them for the installed GPU at run time. No CUDA runtime and no host-side .cu code. Without nvcc
# the CPU colour conversion is used.

set(NITID_PTX_ARCH "compute_75" CACHE STRING "Virtual GPU architecture for Nitid's PTX (compute_75 = RTX 20 and newer)")
set(NITID_HAVE_KERNELS 0)
if(CUDAToolkit_NVCC_EXECUTABLE AND EXISTS "${CUDAToolkit_NVCC_EXECUTABLE}")
  set(NITID_HAVE_KERNELS 1)
  set(_cu ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/kernels/color.cu)
  set(_ptx ${CMAKE_CURRENT_BINARY_DIR}/kernels/color.ptx)
  set(_hdr ${CMAKE_CURRENT_BINARY_DIR}/kernels/nitid_kernels_ptx.h)
  add_custom_command(
    OUTPUT ${_hdr}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/kernels
    COMMAND ${CUDAToolkit_NVCC_EXECUTABLE} -ptx -arch=${NITID_PTX_ARCH} -O3 -o ${_ptx} ${_cu}
    COMMAND ${CMAKE_COMMAND} -DPTX=${_ptx} -DOUT=${_hdr} -DNAME=nitid_kernels_ptx -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ptx.cmake
    DEPENDS ${_cu} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ptx.cmake
    COMMENT "Compiling Nitid CUDA kernels to PTX"
    VERBATIM)
  add_custom_target(nitid_kernels DEPENDS ${_hdr})
else()
  message(WARNING "nvcc not found: GPU colour conversion disabled (CPU fallback)")
endif()

# --- library ----------------------------------------------------------------------

add_library(libnitid
  src/version.cpp
  src/settings.cpp
  src/engine.cpp
  # core: logging, errors, small utilities
  src/core/log.cpp
  # gpu: Vulkan device, CUDA context, Vulkan <-> CUDA sharing
  src/gpu/vulkan_context.cpp
  src/gpu/cuda_context.cpp
  src/gpu/interop.cpp
  src/gpu/kernels.cpp          # embedded PTX kernels (colour conversion)
  # input: frame sources (files now; screen capture and capture cards later)
  src/input/ffmpeg_file_source.cpp
  src/input/scene_cut.cpp
  # vfx: NVIDIA VFX SDK effects
  src/vfx/vfx_api.cpp
  src/vfx/super_res.cpp
  src/vfx/frame_gen.cpp
  # pipeline: upscale -> frame generation, frame selection
  src/pipeline/pipeline.cpp
  # output: window presenter (recording and virtual camera later)
  src/output/swapchain_presenter.cpp
)
add_library(nitid::nitid ALIAS libnitid)

set_target_properties(libnitid PROPERTIES
  OUTPUT_NAME nitid                 # -> libnitid.so
  EXPORT_NAME nitid                 # -> imported target nitid::nitid
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  INSTALL_RPATH "$ORIGIN"           # finds libraries bundled next to it (release tarball: FFmpeg)
  CXX_VISIBILITY_PRESET hidden      # only NITID_EXPORT symbols are public
  VISIBILITY_INLINES_HIDDEN ON
)

target_compile_features(libnitid PUBLIC cxx_std_23)
target_compile_options(libnitid PRIVATE -Wall -Wextra -Wno-missing-field-initializers)  # Vulkan {sType} init idiom

target_compile_definitions(libnitid PRIVATE
  NITID_VERSION_STRING="${PROJECT_VERSION}"
  NITID_VFX_DEFAULT_DIR="${NITID_VFX_RUNTIME_DIR}"
  NITID_HAVE_KERNELS=${NITID_HAVE_KERNELS}
)
if(NITID_HAVE_KERNELS)
  add_dependencies(libnitid nitid_kernels)
  target_include_directories(libnitid PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/kernels)
endif()

target_include_directories(libnitid PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(libnitid SYSTEM PRIVATE
  ${NITID_VFX_INCLUDE_DIR}
)

target_link_libraries(libnitid
  PUBLIC  Vulkan::Vulkan
  PRIVATE CUDA::cuda_driver PkgConfig::FFMPEG Threads::Threads ${CMAKE_DL_LIBS}
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

if(NITID_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libnitidConfigVersion.cmake
  DESTINATION ${LIBNITID_CMAKE_DIR}
)
