#include "ui/overlay.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <print>
#include <stdexcept>

namespace nitid_app {

namespace {

void check_vk(VkResult r)
{
    if (r != VK_SUCCESS) std::println(stderr, "ImGui Vulkan error {}", int(r));
}

void apply_style(float scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.WindowBorderSize = 0;
    s.Colors[ImGuiCol_WindowBg].w = 0.82f;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

} // namespace

Overlay::Overlay(SDL_Window* window, nitid::Engine& engine) : engine_(engine)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini next to the user's files
    // No keyboard navigation: arrows / Space are the player's shortcuts.
    io.Fonts->AddFontDefault();  // scalable default font
    float scale = SDL_GetWindowDisplayScale(window);
    apply_style(scale > 0 ? scale : 1.0f);

    if (!ImGui_ImplSDL3_InitForVulkan(window)) throw std::runtime_error("ImGui SDL3 backend failed");
    nitid::VulkanDevice d = engine_.vulkan_device();
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = d.api_version;
    ii.Instance = d.instance;
    ii.PhysicalDevice = d.physical_device;
    ii.Device = d.device;
    ii.QueueFamily = d.queue_family;
    ii.Queue = d.queue;
    ii.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    ii.MinImageCount = 2;
    ii.ImageCount = d.window_image_count < 2 ? 2 : d.window_image_count;
    ii.UseDynamicRendering = true;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    ii.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    static VkFormat format;  // must outlive Init (pointer kept for pipeline creation)
    format = d.window_format;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
    ii.CheckVkResultFn = check_vk;
    if (!ImGui_ImplVulkan_Init(&ii)) throw std::runtime_error("ImGui Vulkan backend failed");

    engine_.set_overlay([](const nitid::OverlayFrame& f) {
        if (ImDrawData* dd = ImGui::GetDrawData()) ImGui_ImplVulkan_RenderDrawData(dd, f.cmd);
    });
}

Overlay::~Overlay()
{
    engine_.set_overlay(nullptr);
    vkDeviceWaitIdle(engine_.vulkan_device().device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void Overlay::process_event(const SDL_Event& ev) { ImGui_ImplSDL3_ProcessEvent(&ev); }
bool Overlay::wants_keyboard() const { return ImGui::GetIO().WantTextInput; }
bool Overlay::wants_mouse() const { return ImGui::GetIO().WantCaptureMouse; }

void Overlay::begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Overlay::end_frame() { ImGui::Render(); }

} // namespace nitid_app
