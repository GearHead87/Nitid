#include "gpu/vulkan_context.hpp"

#include "core/log.hpp"
#include "gpu/vk_check.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace nitid::gpu {

namespace {

bool contains(const std::vector<std::string>& v, const char* s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

VulkanContext::VulkanContext(const std::vector<std::string>& instance_extensions, bool validation)
{
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> avail(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, avail.data());
    std::vector<std::string> exts = instance_extensions;
    std::sort(exts.begin(), exts.end());
    exts.erase(std::unique(exts.begin(), exts.end()), exts.end());
    std::vector<const char*> names;
    for (auto& e : exts) {
        bool ok = std::any_of(avail.begin(), avail.end(), [&](auto& a) { return e == a.extensionName; });
        if (!ok) failf("Vulkan instance extension not available: {}", e);
        names.push_back(e.c_str());
    }
    std::vector<const char*> layers;
    if (validation) {
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> lp(n);
        vkEnumerateInstanceLayerProperties(&n, lp.data());
        if (std::any_of(lp.begin(), lp.end(), [](auto& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; }))
            layers.push_back("VK_LAYER_KHRONOS_validation");
        else
            log::warn("validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Nitid";
    app.pEngineName = "libnitid";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = uint32_t(names.size());
    ci.ppEnabledExtensionNames = names.data();
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    NITID_VK(vkCreateInstance(&ci, nullptr, &instance));

    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.vendorID == 0x10DE && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            phys = d;
            props = p;
            break;
        }
    }
    if (!phys) {
        vkDestroyInstance(instance, nullptr);
        fail("No NVIDIA GPU found through Vulkan. Nitid needs an NVIDIA RTX GPU and its driver.");
    }
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    VkPhysicalDeviceIDProperties idp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idp};
    vkGetPhysicalDeviceProperties2(phys, &p2);
    std::memcpy(device_uuid.data(), idp.deviceUUID, VK_UUID_SIZE);
    log::info("GPU: {} (driver {})", props.deviceName, driver_version());
}

std::vector<std::string> VulkanContext::available_device_extensions() const
{
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> avail(n);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
    std::vector<std::string> out;
    for (auto& e : avail) out.emplace_back(e.extensionName);
    return out;
}

void VulkanContext::create_device(VkSurfaceKHR surface)
{
    auto avail = available_device_extensions();
    std::vector<const char*> exts = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    for (auto e : exts)
        if (!contains(avail, e)) failf("Vulkan device extension not available: {}", e);
    if (surface) {
        if (!contains(avail, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) fail("Vulkan swapchain extension not available");
        exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        swapchain = true;
        present_wait = contains(avail, VK_KHR_PRESENT_ID_EXTENSION_NAME) && contains(avail, VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        if (present_wait) {
            exts.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            exts.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        }
    }

    // Screen capture: import the compositor's DMA-BUF frames (spike S6).
    dma_buf_import = contains(avail, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) &&
                     contains(avail, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) &&
                     contains(avail, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    if (dma_buf_import) {
        exts.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        exts.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        exts.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    }

    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, qf.data());
    queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        const auto want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((qf[i].queueFlags & want) != want) continue;
        VkBool32 can_present = VK_TRUE;
        if (surface) vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &can_present);
        if (can_present) {
            queue_family = i;
            break;
        }
    }
    if (queue_family == UINT32_MAX) fail("No Vulkan queue that can draw and present to the window");

    VkPhysicalDevicePresentWaitFeaturesKHR pw{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDevicePresentIdFeaturesKHR pid{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &pw};
    pw.presentWait = VK_TRUE;
    pid.presentId = VK_TRUE;
    // Two queues of the family when it has them: the render thread's, and one for the capture thread (Vulkan
    // queues must not be used from two threads at once).
    const uint32_t queues = std::min(2u, qf[queue_family].queueCount);
    float prio[2] = {1.0f, 0.5f};
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = queues;
    qci.pQueuePriorities = prio;
    // Dynamic rendering (Vulkan 1.3): the app's overlay (ImGui) draws onto the window image without render passes.
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.pNext = present_wait ? &pid : nullptr;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &v13;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = uint32_t(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    NITID_VK(vkCreateDevice(phys, &ci, nullptr, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    if (queues > 1) vkGetDeviceQueue(device, queue_family, 1, &capture_queue);
    else capture_queue = queue;

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queue_family;
    NITID_VK(vkCreateCommandPool(device, &pci, nullptr, &pool));
}

VulkanContext::~VulkanContext()
{
    if (device) {
        vkDeviceWaitIdle(device);
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

uint32_t VulkanContext::memory_type(uint32_t bits, VkMemoryPropertyFlags want) const
{
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want) return i;
    failf("No Vulkan memory type for bits {:#x} flags {:#x}", bits, want);
}

std::string VulkanContext::driver_version() const
{
    uint32_t v = props.driverVersion;  // NVIDIA packing: 10.8.8.6
    return std::format("{}.{}.{:02}", v >> 22, (v >> 14) & 0xff, (v >> 6) & 0xff);
}

VkCommandBuffer VulkanContext::begin_once()
{
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    NITID_VK(vkAllocateCommandBuffers(device, &ai, &cb));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NITID_VK(vkBeginCommandBuffer(cb, &bi));
    return cb;
}

void VulkanContext::submit_once(VkCommandBuffer cb)
{
    NITID_VK(vkEndCommandBuffer(cb));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    NITID_VK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    NITID_VK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, pool, 1, &cb);
}

} // namespace nitid::gpu
