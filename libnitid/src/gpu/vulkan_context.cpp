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

void VulkanContext::create_device(VkSurfaceKHR surface, const std::vector<std::string>& optional)
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

    // DLSS (NGX) needs a handful of NVIDIA extensions; all or nothing.
    optional_extensions = !optional.empty() &&
                          std::all_of(optional.begin(), optional.end(), [&](auto& e) { return contains(avail, e.c_str()); });
    bool buffer_address = false;
    if (optional_extensions)
        for (auto& e : optional) {
            if (std::none_of(exts.begin(), exts.end(), [&](const char* x) { return e == x; })) exts.push_back(e.c_str());
            if (e == VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) buffer_address = true;
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
    // Hardware optical flow (motion vectors for DLSS; spike S4): extension + a queue family that runs it.
    if (contains(avail, VK_NV_OPTICAL_FLOW_EXTENSION_NAME))
        for (uint32_t i = 0; i < n; ++i)
            if (qf[i].queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) {
                flow_family = i;
                break;
            }
    optical_flow = flow_family != UINT32_MAX;
    if (optical_flow) exts.push_back(VK_NV_OPTICAL_FLOW_EXTENSION_NAME);  // needs synchronization2 (core 1.3)

    VkPhysicalDevicePresentWaitFeaturesKHR pw{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDevicePresentIdFeaturesKHR pid{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &pw};
    pw.presentWait = VK_TRUE;
    pid.presentId = VK_TRUE;
    // Three queues of the family: the render thread's, the capture thread's and the pipeline thread's (Vulkan
    // queues must not be used from two threads at once). NVIDIA GPUs have 16; with fewer, work_queue stays null
    // and the pipeline's Vulkan users (DLSS, optical flow) stay off.
    const uint32_t queues = std::min(3u, qf[queue_family].queueCount);
    float prio[3] = {1.0f, 0.5f, 0.5f};
    VkDeviceQueueCreateInfo qcis[2]{{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}, {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}};
    qcis[0].queueFamilyIndex = queue_family;
    qcis[0].queueCount = queues;
    qcis[0].pQueuePriorities = prio;
    qcis[1].queueFamilyIndex = flow_family;
    qcis[1].queueCount = 1;
    qcis[1].pQueuePriorities = prio + 1;
    // Dynamic rendering (Vulkan 1.3): the app's overlay (ImGui) draws onto the window image without render passes.
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    v13.pNext = present_wait ? &pid : nullptr;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13};
    v12.bufferDeviceAddress = buffer_address ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceOpticalFlowFeaturesNV off{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV, &v12};
    off.opticalFlow = VK_TRUE;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = optical_flow ? static_cast<void*>(&off) : static_cast<void*>(&v12);
    ci.queueCreateInfoCount = optical_flow ? 2 : 1;
    ci.pQueueCreateInfos = qcis;
    ci.enabledExtensionCount = uint32_t(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    NITID_VK(vkCreateDevice(phys, &ci, nullptr, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    if (queues > 1) vkGetDeviceQueue(device, queue_family, 1, &capture_queue);
    else capture_queue = queue;
    if (queues > 2) vkGetDeviceQueue(device, queue_family, 2, &work_queue);
    if (optical_flow) vkGetDeviceQueue(device, flow_family, 0, &flow_queue);

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
