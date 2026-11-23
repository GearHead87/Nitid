// Minimal Vulkan setup shared by the spikes. Header-only, spike quality:
// aborts with a message on any failure.
#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define VK_CHECK(x)                                                                    \
    do {                                                                               \
        VkResult r_ = (x);                                                             \
        if (r_ != VK_SUCCESS) {                                                        \
            std::fprintf(stderr, "%s:%d: %s failed: %d\n", __FILE__, __LINE__, #x, r_); \
            std::exit(1);                                                              \
        }                                                                              \
    } while (0)

namespace spike {

inline bool has_ext(const std::vector<VkExtensionProperties>& v, const char* name)
{
    return std::any_of(v.begin(), v.end(), [&](auto& e) { return std::strcmp(e.extensionName, name) == 0; });
}

struct VkCtx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceMemoryProperties mem{};
    std::vector<std::string> instance_exts, device_exts;

    // Pass the extra extensions to enable (e.g. what NGX asks for). Unavailable ones abort.
    void create_instance(std::vector<std::string> exts, uint32_t api = VK_API_VERSION_1_3)
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> avail(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, avail.data());
        std::sort(exts.begin(), exts.end());
        exts.erase(std::unique(exts.begin(), exts.end()), exts.end());
        std::vector<const char*> names;
        for (auto& e : exts) {
            if (!has_ext(avail, e.c_str())) {
                std::fprintf(stderr, "instance extension missing: %s\n", e.c_str());
                std::exit(1);
            }
            names.push_back(e.c_str());
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "nitid-spike";
        app.apiVersion = api;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = uint32_t(names.size());
        ci.ppEnabledExtensionNames = names.data();
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
        instance_exts = exts;
    }

    // Pick the NVIDIA GPU explicitly (this PC also has an AMD iGPU).
    void pick_nvidia()
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, nullptr);
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance, &n, devs.data());
        for (auto d : devs) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(d, &p);
            if (p.vendorID == 0x10DE) {
                phys = d;
                props = p;
                vkGetPhysicalDeviceMemoryProperties(d, &mem);
                return;
            }
        }
        std::fprintf(stderr, "no NVIDIA Vulkan device found\n");
        std::exit(1);
    }

    // Creates the device with every supported core 1.1/1.2/1.3 feature enabled (fine for spikes)
    // plus the requested extensions, and one graphics+compute queue.
    void create_device(std::vector<std::string> exts, const void* extra_features_chain = nullptr)
    {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> avail(n);
        vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
        std::sort(exts.begin(), exts.end());
        exts.erase(std::unique(exts.begin(), exts.end()), exts.end());
        std::vector<const char*> names;
        for (auto& e : exts) {
            if (!has_ext(avail, e.c_str())) {
                std::fprintf(stderr, "device extension missing: %s\n", e.c_str());
                std::exit(1);
            }
            names.push_back(e.c_str());
        }

        vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qf(n);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, qf.data());
        queue_family = UINT32_MAX;
        for (uint32_t i = 0; i < n; ++i)
            if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                queue_family = i;
                break;
            }
        if (queue_family == UINT32_MAX) {
            std::fprintf(stderr, "no graphics+compute queue\n");
            std::exit(1);
        }

        v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        v13.pNext = const_cast<void*>(extra_features_chain);
        v12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13};
        v11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &v12};
        f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v11};
        vkGetPhysicalDeviceFeatures2(phys, &f2);

        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queue_family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.pNext = &f2;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qci;
        ci.enabledExtensionCount = uint32_t(names.size());
        ci.ppEnabledExtensionNames = names.data();
        VK_CHECK(vkCreateDevice(phys, &ci, nullptr, &device));
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        device_exts = exts;

        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = queue_family;
        VK_CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));
    }

    VkCommandBuffer begin()
    {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cb));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));
        return cb;
    }

    void submit_wait(VkCommandBuffer cb)
    {
        VK_CHECK(vkEndCommandBuffer(cb));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device, pool, 1, &cb);
    }

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) const
    {
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want)
                return i;
        std::fprintf(stderr, "no memory type for bits 0x%x flags 0x%x\n", bits, want);
        std::exit(1);
    }

    std::string driver_version() const
    {
        uint32_t v = props.driverVersion;  // NVIDIA packing: 10.8.8.6
        return std::to_string(v >> 22) + "." + std::to_string((v >> 14) & 0xff) + "." +
               std::to_string((v >> 6) & 0xff);
    }

    void destroy()
    {
        if (device) {
            vkDeviceWaitIdle(device);
            vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }

private:
    VkPhysicalDeviceFeatures2 f2{};
    VkPhysicalDeviceVulkan11Features v11{};
    VkPhysicalDeviceVulkan12Features v12{};
    VkPhysicalDeviceVulkan13Features v13{};
};

}  // namespace spike
