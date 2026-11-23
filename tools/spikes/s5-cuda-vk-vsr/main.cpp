// S5: Vulkan -> CUDA -> VFX Video Super Resolution -> Vulkan, with no CPU copy in the loop.
//
// Frame path per iteration (all on the GPU):
//   Vulkan image (low-res RGBA8) --vkCmdCopyImageToBuffer--> exportable Vulkan buffer (= CUDA pointer)
//   --[Vulkan semaphore A -> CUDA wait]--> NvVFX_Run(VideoSuperRes) on a CUDA stream
//   --> exportable output buffer --[CUDA signal -> Vulkan semaphore B]--> vkCmdCopyBufferToImage
//   --> Vulkan image (high-res RGBA8).
// Memory and semaphores are shared via opaque file descriptors (VK_KHR_external_memory_fd /
// VK_KHR_external_semaphore_fd + cuImportExternalMemory / cuImportExternalSemaphore).
// The CUDA device is matched to the Vulkan device by UUID (this PC also has an AMD iGPU).
//
// Usage: s5-cuda-vk-vsr <in.ppm> <out.ppm> <scale 2|3|4> <quality 0..23> [iterations=200]

#include "ppm.hpp"
#include "vk_ctx.hpp"
#include "vk_image.hpp"

#include <cuda.h>
#include <nvVideoEffects.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace spike;

#define CU_CHECK(x)                                                                         \
    do {                                                                                    \
        CUresult r_ = (x);                                                                  \
        if (r_ != CUDA_SUCCESS) {                                                           \
            const char* s_ = nullptr;                                                       \
            cuGetErrorString(r_, &s_);                                                      \
            std::fprintf(stderr, "%s:%d: %s failed: %d %s\n", __FILE__, __LINE__, #x, r_, s_); \
            std::exit(1);                                                                   \
        }                                                                                   \
    } while (0)
#define CV_CHECK(x)                                                                                   \
    do {                                                                                              \
        NvCV_Status s_ = (x);                                                                         \
        if (s_ != NVCV_SUCCESS) {                                                                     \
            std::fprintf(stderr, "%s:%d: %s failed: %d %s\n", __FILE__, __LINE__, #x, int(s_),        \
                         NvCV_GetErrorStringFromCode(s_));                                            \
            std::exit(1);                                                                             \
        }                                                                                             \
    } while (0)

struct SharedBuffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0, alloc = 0;
    CUexternalMemory ext = nullptr;
    CUdeviceptr ptr = 0;
};

static SharedBuffer make_shared_buffer(VkCtx& vk, VkDeviceSize size)
{
    SharedBuffer b;
    b.size = size;
    VkExternalMemoryBufferCreateInfo ebi{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    ebi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &ebi};
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateBuffer(vk.device, &ci, nullptr, &b.buf));
    VkMemoryRequirements r;
    vkGetBufferMemoryRequirements(vk.device, b.buf, &r);
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.buffer = b.buf;
    VkExportMemoryAllocateInfo exp{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &ded};
    exp.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exp};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(vk.device, &ai, nullptr, &b.mem));
    VK_CHECK(vkBindBufferMemory(vk.device, b.buf, b.mem, 0));
    b.alloc = r.size;

    auto getFd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdKHR"));
    VkMemoryGetFdInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    gi.memory = b.mem;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    VK_CHECK(getFd(vk.device, &gi, &fd));
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC md{};
    md.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    md.handle.fd = fd;  // ownership passes to CUDA
    md.size = r.size;
    md.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
    CU_CHECK(cuImportExternalMemory(&b.ext, &md));
    CUDA_EXTERNAL_MEMORY_BUFFER_DESC bd{};
    bd.offset = 0;
    bd.size = size;
    CU_CHECK(cuExternalMemoryGetMappedBuffer(&b.ptr, b.ext, &bd));
    return b;
}

struct SharedSemaphore {
    VkSemaphore sem = VK_NULL_HANDLE;
    CUexternalSemaphore ext = nullptr;
};

static SharedSemaphore make_shared_semaphore(VkCtx& vk)
{
    SharedSemaphore s;
    VkExportSemaphoreCreateInfo es{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    es.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &es};
    VK_CHECK(vkCreateSemaphore(vk.device, &ci, nullptr, &s.sem));
    auto getFd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetSemaphoreFdKHR"));
    VkSemaphoreGetFdInfoKHR gi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    gi.semaphore = s.sem;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    VK_CHECK(getFd(vk.device, &gi, &fd));
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC sd{};
    sd.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
    sd.handle.fd = fd;
    CU_CHECK(cuImportExternalSemaphore(&s.ext, &sd));
    return s;
}

static void submit(VkCtx& vk, VkCommandBuffer cb, VkSemaphore wait, VkSemaphore signal, VkFence fence)
{
    VK_CHECK(vkEndCommandBuffer(cb));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = wait ? 1 : 0;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = signal ? 1 : 0;
    si.pSignalSemaphores = &signal;
    VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, fence));
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s in.ppm out.ppm scale quality [iterations]\n", argv[0]);
        return 2;
    }
    uint32_t w, h;
    auto px = read_ppm_rgba(argv[1], w, h);
    uint32_t scale = std::stoul(argv[3]), quality = std::stoul(argv[4]);
    int iters = argc > 5 ? std::stoi(argv[5]) : 200;
    uint32_t ow = w * scale, oh = h * scale;

    VkCtx vk;
    vk.create_instance({});
    vk.pick_nvidia();
    vk.create_device({VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME});

    // Match the CUDA device to the Vulkan device by UUID.
    VkPhysicalDeviceIDProperties idp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idp};
    vkGetPhysicalDeviceProperties2(vk.phys, &p2);
    CU_CHECK(cuInit(0));
    int ndev = 0;
    CU_CHECK(cuDeviceGetCount(&ndev));
    CUdevice cudev = -1;
    for (int i = 0; i < ndev; ++i) {
        CUdevice d;
        CUuuid u;
        CU_CHECK(cuDeviceGet(&d, i));
        CU_CHECK(cuDeviceGetUuid(&u, d));
        if (std::memcmp(u.bytes, idp.deviceUUID, VK_UUID_SIZE) == 0) cudev = d;
    }
    if (cudev < 0) {
        std::fprintf(stderr, "no CUDA device matches the Vulkan device UUID\n");
        return 1;
    }
    CUcontext ctx;
    CU_CHECK(cuDevicePrimaryCtxRetain(&ctx, cudev));
    CU_CHECK(cuCtxSetCurrent(ctx));
    CUstream stream;
    CU_CHECK(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));
    unsigned vfxv = 0;
    NvVFX_GetVersion(&vfxv);
    std::printf("Vulkan: %s | CUDA device %d (UUID match) | VFX SDK version 0x%08x\n", vk.props.deviceName, int(cudev), vfxv);

    // Vulkan resources.
    Image src = make_image(vk, w, h, VK_FORMAT_R8G8B8A8_UNORM);
    Image dst = make_image(vk, ow, oh, VK_FORMAT_R8G8B8A8_UNORM);
    Buffer up = make_host_buffer(vk, VkDeviceSize(w) * h * 4), down = make_host_buffer(vk, VkDeviceSize(ow) * oh * 4);
    SharedBuffer in = make_shared_buffer(vk, VkDeviceSize(w) * h * 4);
    SharedBuffer out = make_shared_buffer(vk, VkDeviceSize(ow) * oh * 4);
    SharedSemaphore semA = make_shared_semaphore(vk), semB = make_shared_semaphore(vk);
    VkFence fence;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(vk.device, &fci, nullptr, &fence));
    {
        auto cb = vk.begin();
        std::memcpy(up.map, px.data(), px.size());
        cmd_upload(cb, up, src);  // the "decoded frame" lives in a Vulkan image from here on
        to_layout(cb, dst, VK_IMAGE_LAYOUT_GENERAL);
        vk.submit_wait(cb);
    }

    // VFX effect on the shared CUDA pointers.
    NvVFX_Handle fx = nullptr;
    CV_CHECK(NvVFX_CreateEffect("VideoSuperRes", &fx));
    NvCVImage cin{}, cout{};
    CV_CHECK(NvCVImage_Init(&cin, w, h, int(w * 4), reinterpret_cast<void*>(in.ptr), NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_GPU));
    CV_CHECK(NvCVImage_Init(&cout, ow, oh, int(ow * 4), reinterpret_cast<void*>(out.ptr), NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_GPU));
    CV_CHECK(NvVFX_SetU32(fx, "ImageEncodingMode", 0));
    CV_CHECK(NvVFX_SetImage(fx, NVVFX_INPUT_IMAGE, &cin));
    CV_CHECK(NvVFX_SetImage(fx, NVVFX_OUTPUT_IMAGE, &cout));
    CV_CHECK(NvVFX_SetU32(fx, "QualityLevel", quality));
    CV_CHECK(NvVFX_SetF32(fx, "Strength", 1.0f));
    CV_CHECK(NvVFX_SetCudaStream(fx, NVVFX_CUDA_STREAM, stream));
    CV_CHECK(NvVFX_Load(fx));

    CUevent e0, e1;
    CU_CHECK(cuEventCreate(&e0, CU_EVENT_DEFAULT));
    CU_CHECK(cuEventCreate(&e1, CU_EVENT_DEFAULT));
    std::vector<double> vsr_ms, total_ms;
    for (int it = 0; it < iters + 10; ++it) {
        auto t0 = std::chrono::steady_clock::now();
        // 1. Vulkan: image -> shared input buffer, signal A.
        auto cb = vk.begin();
        to_layout(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cb, src.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, in.buf, 1, &c);
        to_layout(cb, src, VK_IMAGE_LAYOUT_GENERAL);
        submit(vk, cb, VK_NULL_HANDLE, semA.sem, VK_NULL_HANDLE);

        // 2. CUDA: wait A, run VSR, signal B.
        CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wp{};
        CU_CHECK(cuWaitExternalSemaphoresAsync(&semA.ext, &wp, 1, stream));
        CU_CHECK(cuEventRecord(e0, stream));
        CV_CHECK(NvVFX_Run(fx, 1));
        CU_CHECK(cuEventRecord(e1, stream));
        CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS sp{};
        CU_CHECK(cuSignalExternalSemaphoresAsync(&semB.ext, &sp, 1, stream));

        // 3. Vulkan: wait B, shared output buffer -> image.
        auto cb2 = vk.begin();
        to_layout(cb2, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy c2{};
        c2.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c2.imageExtent = {ow, oh, 1};
        vkCmdCopyBufferToImage(cb2, out.buf, dst.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c2);
        to_layout(cb2, dst, VK_IMAGE_LAYOUT_GENERAL);
        submit(vk, cb2, semB.sem, VK_NULL_HANDLE, fence);
        VK_CHECK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(vk.device, 1, &fence));
        auto t1 = std::chrono::steady_clock::now();
        float ms = 0;
        CU_CHECK(cuEventElapsedTime(&ms, e0, e1));
        if (it >= 10) {
            vsr_ms.push_back(ms);
            total_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
    }
    VK_CHECK(vkDeviceWaitIdle(vk.device));
    // Read back the final Vulkan image (verification only; not part of the loop).
    {
        auto cb = vk.begin();
        cmd_download(cb, dst, down);
        vk.submit_wait(cb);
        write_ppm_rgba(argv[2], static_cast<uint8_t*>(down.map), ow, oh);
    }
    std::sort(vsr_ms.begin(), vsr_ms.end());
    std::sort(total_ms.begin(), total_ms.end());
    std::printf("RESULT %ux%u -> %ux%u quality=%u iters=%d vsr_ms_median=%.3f round_trip_ms_median=%.3f round_trip_ms_p90=%.3f\n",
                w, h, ow, oh, quality, iters, vsr_ms[vsr_ms.size() / 2], total_ms[total_ms.size() / 2],
                total_ms[total_ms.size() * 9 / 10]);

    NvVFX_DestroyEffect(fx);
    cuDestroyExternalSemaphore(semA.ext);
    cuDestroyExternalSemaphore(semB.ext);
    cuMemFree(in.ptr);
    cuMemFree(out.ptr);
    cuDestroyExternalMemory(in.ext);
    cuDestroyExternalMemory(out.ext);
    vkDestroySemaphore(vk.device, semA.sem, nullptr);
    vkDestroySemaphore(vk.device, semB.sem, nullptr);
    vkDestroyFence(vk.device, fence, nullptr);
    vkDestroyBuffer(vk.device, in.buf, nullptr);
    vkFreeMemory(vk.device, in.mem, nullptr);
    vkDestroyBuffer(vk.device, out.buf, nullptr);
    vkFreeMemory(vk.device, out.mem, nullptr);
    destroy(vk, src);
    destroy(vk, dst);
    destroy(vk, up);
    destroy(vk, down);
    cuStreamDestroy(stream);
    cuDevicePrimaryCtxRelease(cudev);
    vk.destroy();
    return 0;
}
