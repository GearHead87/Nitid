#include "gpu/cuda_context.hpp"

#include "core/check.hpp"
#include "core/log.hpp"

#include <cstring>
#include <utility>

namespace nitid::gpu {

void cuda_fail(const char* what, CUresult r)
{
    const char* s = nullptr;
    cuGetErrorString(r, &s);
    failf("CUDA: {} failed ({} {})", what, int(r), s ? s : "?");
}

CudaContext::CudaContext(const std::array<uint8_t, 16>& uuid)
{
    NITID_CU(cuInit(0));
    int n = 0;
    NITID_CU(cuDeviceGetCount(&n));
    for (int i = 0; i < n; ++i) {
        CUdevice d;
        CUuuid u;
        NITID_CU(cuDeviceGet(&d, i));
        NITID_CU(cuDeviceGetUuid(&u, d));
        if (std::memcmp(u.bytes, uuid.data(), 16) == 0) device = d;
    }
    if (device < 0) fail("No CUDA device matches the Vulkan GPU");
    NITID_CU(cuDevicePrimaryCtxRetain(&ctx, device));
    NITID_CU(cuCtxSetCurrent(ctx));
    // A *blocking* stream (synchronises with CUDA's legacy default stream). The VFX SDK does part of its
    // work on the default stream; with a non-blocking stream VSR returned the previous frame's result
    // (tests/v0.1/02-upscaling).
    // Lowest priority: the presenter's copies (own stream, highest priority) go first, so a long frame-generation
    // burst doesn't make the display miss a refresh (tests/v0.3/01).
    int least = 0, greatest = 0;
    NITID_CU(cuCtxGetStreamPriorityRange(&least, &greatest));
    NITID_CU(cuStreamCreateWithPriority(&stream, CU_STREAM_DEFAULT, least));
    log::debug("CUDA device {} matched to the Vulkan GPU", int(device));
}

CudaContext::~CudaContext()
{
    if (ctx) {
        cuCtxSetCurrent(ctx);
        if (stream) {
            cuStreamSynchronize(stream);
            cuStreamDestroy(stream);
        }
        cuDevicePrimaryCtxRelease(device);
    }
}

void CudaContext::make_current() const { NITID_CU(cuCtxSetCurrent(ctx)); }
void CudaContext::sync() const { NITID_CU(cuStreamSynchronize(stream)); }

CudaBuffer::CudaBuffer(size_t bytes) : size_(bytes) { NITID_CU(cuMemAlloc(&ptr_, bytes)); }
CudaBuffer::~CudaBuffer()
{
    if (ptr_) cuMemFree(ptr_);
}
CudaBuffer::CudaBuffer(CudaBuffer&& o) noexcept : ptr_(std::exchange(o.ptr_, 0)), size_(std::exchange(o.size_, 0)) {}
CudaBuffer& CudaBuffer::operator=(CudaBuffer&& o) noexcept
{
    if (this != &o) {
        if (ptr_) cuMemFree(ptr_);
        ptr_ = std::exchange(o.ptr_, 0);
        size_ = std::exchange(o.size_, 0);
    }
    return *this;
}

} // namespace nitid::gpu
