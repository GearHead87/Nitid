#pragma once

// CUDA (driver API) on the same GPU as the Vulkan device, matched by UUID. The VFX SDK runs on CUDA.
#include <cuda.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace nitid::gpu {

#define NITID_CU(x)                                                                          \
    do {                                                                                     \
        CUresult r_ = (x);                                                                   \
        if (r_ != CUDA_SUCCESS) ::nitid::gpu::cuda_fail(#x, r_);                             \
    } while (0)

[[noreturn]] void cuda_fail(const char* what, CUresult r);

class CudaContext {
public:
    explicit CudaContext(const std::array<uint8_t, 16>& vulkan_device_uuid);
    ~CudaContext();
    CudaContext(const CudaContext&) = delete;
    CudaContext& operator=(const CudaContext&) = delete;

    void make_current() const;  // call on any thread before using CUDA there
    void sync() const;          // wait for all work on the stream

    CUdevice device = -1;
    CUcontext ctx = nullptr;
    CUstream stream = nullptr;
};

// Plain CUDA device memory (not shared with Vulkan).
class CudaBuffer {
public:
    CudaBuffer() = default;
    explicit CudaBuffer(size_t bytes);
    ~CudaBuffer();
    CudaBuffer(CudaBuffer&& o) noexcept;
    CudaBuffer& operator=(CudaBuffer&& o) noexcept;

    CUdeviceptr ptr() const { return ptr_; }
    size_t size() const { return size_; }
    explicit operator bool() const { return ptr_ != 0; }

private:
    CUdeviceptr ptr_ = 0;
    size_t size_ = 0;
};

} // namespace nitid::gpu
