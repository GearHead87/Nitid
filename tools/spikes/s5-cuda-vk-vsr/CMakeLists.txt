# Needs: CUDA toolkit (cuda.h + libcuda), the VFX SDK C headers (scripts/fetch-sdks.sh) and the
# VFX runtime libraries from the nvidia-vfx wheel (tools/spikes/vfx-env/setup.sh).
set(VFX_INC "${NITID_ROOT}/third_party/Maxine-VFX-SDK/nvvfx/include")
set(VFX_LIBDIR "${NITID_ROOT}/third_party/vfx-venv/lib/python3.12/site-packages/nvvfx/libs")
if(NOT EXISTS "${VFX_INC}/nvVideoEffects.h" OR NOT EXISTS "${VFX_LIBDIR}/libVideoFX.so")
  message(STATUS "s5-cuda-vk-vsr skipped: VFX headers or runtime missing")
  return()
endif()
find_package(CUDAToolkit REQUIRED)
add_executable(s5-cuda-vk-vsr main.cpp)
target_include_directories(s5-cuda-vk-vsr PRIVATE "${VFX_INC}")
target_link_libraries(s5-cuda-vk-vsr PRIVATE spike_common CUDA::cuda_driver
  "${VFX_LIBDIR}/libVideoFX.so" "${VFX_LIBDIR}/libNVCVImage.so")
set_target_properties(s5-cuda-vk-vsr PROPERTIES BUILD_RPATH "${VFX_LIBDIR}")
