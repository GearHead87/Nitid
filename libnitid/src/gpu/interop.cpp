#include "gpu/interop.hpp"

#include "gpu/vk_check.hpp"

namespace nitid::gpu {

SharedBuffer::SharedBuffer(VulkanContext& vk, VkDeviceSize size) : vk_(vk), size_(size)
{
    VkExternalMemoryBufferCreateInfo ebi{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    ebi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &ebi};
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    NITID_VK(vkCreateBuffer(vk.device, &ci, nullptr, &buf_));
    VkMemoryRequirements r;
    vkGetBufferMemoryRequirements(vk.device, buf_, &r);
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.buffer = buf_;
    VkExportMemoryAllocateInfo exp{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &ded};
    exp.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exp};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = vk.memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    NITID_VK(vkAllocateMemory(vk.device, &ai, nullptr, &mem_));
    NITID_VK(vkBindBufferMemory(vk.device, buf_, mem_, 0));

    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdKHR"));
    VkMemoryGetFdInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    gi.memory = mem_;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    NITID_VK(get_fd(vk.device, &gi, &fd));
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC md{};
    md.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    md.handle.fd = fd;  // CUDA takes ownership of the fd
    md.size = r.size;
    md.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
    NITID_CU(cuImportExternalMemory(&ext_, &md));
    CUDA_EXTERNAL_MEMORY_BUFFER_DESC bd{};
    bd.size = size;
    NITID_CU(cuExternalMemoryGetMappedBuffer(&ptr_, ext_, &bd));
}

SharedBuffer::~SharedBuffer()
{
    if (ptr_) cuMemFree(ptr_);
    if (ext_) cuDestroyExternalMemory(ext_);
    if (buf_) vkDestroyBuffer(vk_.device, buf_, nullptr);
    if (mem_) vkFreeMemory(vk_.device, mem_, nullptr);
}

SharedSemaphore::SharedSemaphore(VulkanContext& vk) : vk_(vk)
{
    VkExportSemaphoreCreateInfo es{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    es.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &es};
    NITID_VK(vkCreateSemaphore(vk.device, &ci, nullptr, &sem_));
    auto get_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetSemaphoreFdKHR"));
    VkSemaphoreGetFdInfoKHR gi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    gi.semaphore = sem_;
    gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    NITID_VK(get_fd(vk.device, &gi, &fd));
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC sd{};
    sd.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
    sd.handle.fd = fd;
    NITID_CU(cuImportExternalSemaphore(&ext_, &sd));
}

SharedSemaphore::~SharedSemaphore()
{
    if (ext_) cuDestroyExternalSemaphore(ext_);
    if (sem_) vkDestroySemaphore(vk_.device, sem_, nullptr);
}

void SharedSemaphore::cuda_signal(CUstream s)
{
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS p{};
    NITID_CU(cuSignalExternalSemaphoresAsync(&ext_, &p, 1, s));
}

void SharedSemaphore::cuda_wait(CUstream s)
{
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS p{};
    NITID_CU(cuWaitExternalSemaphoresAsync(&ext_, &p, 1, s));
}

} // namespace nitid::gpu
