# Spikes: standalone feasibility programs. Built separately from Nitid:
#   cmake -S tools/spikes -B build-spikes -G Ninja && cmake --build build-spikes
# Needs scripts/fetch-sdks.sh (DLSS SDK in third_party/DLSS).

cmake_minimum_required(VERSION 3.25)
project(nitid_spikes LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(NITID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(NITID_DLSS_SDK "${NITID_ROOT}/third_party/DLSS" CACHE PATH "NVIDIA DLSS SDK checkout")

find_package(Vulkan REQUIRED)

# NVIDIA NGX (DLSS SDK) static loader library + headers.
add_library(ngx STATIC IMPORTED GLOBAL)
set_target_properties(ngx PROPERTIES
  IMPORTED_LOCATION "${NITID_DLSS_SDK}/lib/Linux_x86_64/libnvsdk_ngx.a"
  INTERFACE_INCLUDE_DIRECTORIES "${NITID_DLSS_SDK}/include")
target_link_libraries(ngx INTERFACE Vulkan::Vulkan ${CMAKE_DL_LIBS})

# Shared spike helpers (Vulkan setup, image I/O).
add_library(spike_common INTERFACE)
target_include_directories(spike_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_link_libraries(spike_common INTERFACE Vulkan::Vulkan)
target_compile_definitions(spike_common INTERFACE
  NITID_ROOT="${NITID_ROOT}"
  NITID_DLSS_LIB_DIR="${NITID_DLSS_SDK}/lib/Linux_x86_64/rel")

# Each spike adds itself when its directory has a CMakeLists.txt.
file(GLOB spike_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/s*-*")
foreach(d IN LISTS spike_dirs)
  if(EXISTS "${d}/CMakeLists.txt")
    add_subdirectory("${d}")
  endif()
endforeach()
