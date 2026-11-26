#pragma once

// NVIDIA VFX SDK loaded at runtime (dlopen), so Nitid still starts when the runtime is missing and
// can report "AI effects unavailable" instead of failing to launch.
//
// The runtime is the folder with libVideoFX.so, libNVCVImage.so, the NPP/cuDNN libraries and the
// effect libraries (today: the nvvfx/libs folder of NVIDIA's nvidia-vfx wheel; see tools/spikes/vfx-env).
#include <nvVideoEffects.h>

#include <memory>
#include <string>

namespace nitid::vfx {

struct Api {
    decltype(&NvVFX_GetVersion) GetVersion = nullptr;
    decltype(&NvVFX_CreateEffect) CreateEffect = nullptr;
    decltype(&NvVFX_DestroyEffect) DestroyEffect = nullptr;
    decltype(&NvVFX_SetU32) SetU32 = nullptr;
    decltype(&NvVFX_SetF32) SetF32 = nullptr;
    decltype(&NvVFX_SetImage) SetImage = nullptr;
    decltype(&NvVFX_SetCudaStream) SetCudaStream = nullptr;
    decltype(&NvVFX_Load) Load = nullptr;
    decltype(&NvVFX_Run) Run = nullptr;
    decltype(&NvCVImage_Init) ImageInit = nullptr;
    decltype(&NvCV_GetErrorStringFromCode) ErrorString = nullptr;

    std::string dir;      // where the runtime was loaded from
    std::string version;  // e.g. "1.3.0.0"

    // Throws nitid::Error with the effect/call name and the SDK's message.
    void check(NvCV_Status s, const char* what) const;
};

// Finds and loads the runtime. Search order: explicit_dir, $NITID_VFX_DIR, then nitid-setup-vfx installs
// ($XDG_DATA_HOME/nitid/vfx/nvvfx/libs, <libnitid dir>/nitid/vfx/nvvfx/libs), the build-time default (developers).
// Returns nullptr and sets *error if no usable runtime was found.
std::shared_ptr<const Api> load_api(const std::string& explicit_dir, std::string* error);

// An RGBA8 GPU image view for VFX calls (chunky, pitch = width * 4).
NvCVImage rgba_image(const Api& api, void* gpu_ptr, unsigned width, unsigned height);

} // namespace nitid::vfx
