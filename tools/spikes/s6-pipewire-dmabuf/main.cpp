// S6: Can we capture the screen on KDE Wayland via xdg-desktop-portal + PipeWire and import the
// frames into Vulkan as DMA-BUF (no CPU copy)?
//
// 1. libportal ScreenCast session (monitor, cursor embedded, persistent). The first run shows KDE's
//    permission dialog; the restore token is saved so later runs don't ask again.
// 2. PipeWire stream on the portal's node. We offer BGRx/BGRA with the DRM format modifiers that
//    Vulkan can import (VK_EXT_image_drm_format_modifier), plus a shared-memory fallback.
// 3. For each DMA-BUF frame: import into a VkImage (VK_EXT_external_memory_dma_buf), copy to a host
//    buffer (verification only) and save the first frame as PPM.
//
// Usage: s6-pipewire-dmabuf <out_dir> [frames=60] [timeout_s=60]

#include "ppm.hpp"
#include "vk_ctx.hpp"
#include "vk_image.hpp"

#include <libportal/portal.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>
#include <spa/debug/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace spike;

struct App {
    std::string out;
    int want_frames = 60;
    // portal
    GMainLoop* gloop = nullptr;
    XdpSession* session = nullptr;
    int pw_fd = -1;
    uint32_t node = 0;
    std::string error;
    // vulkan
    VkCtx vk;
    std::vector<uint64_t> mods_bgra;
    // pipewire
    pw_main_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    spa_hook listener{};
    spa_video_info_raw fmt{};
    bool have_mod = false;
    // stats
    int frames = 0, dmabuf_frames = 0, shm_frames = 0, imported = 0, import_fail = 0;
    double import_ms_sum = 0;
    bool saved = false;
    std::chrono::steady_clock::time_point t_first;
    double span_ms = 0;
};

// ---------------- portal ----------------
static void on_started(GObject* src, GAsyncResult* res, gpointer data)
{
    auto* a = static_cast<App*>(data);
    GError* err = nullptr;
    if (!xdp_session_start_finish(XDP_SESSION(src), res, &err)) {
        a->error = std::string("session start failed: ") + (err ? err->message : "?");
        g_main_loop_quit(a->gloop);
        return;
    }
    if (char* tok = xdp_session_get_restore_token(a->session)) {
        std::ofstream(a->out + "/restore_token.txt") << tok;
        g_free(tok);
    }
    a->pw_fd = xdp_session_open_pipewire_remote(a->session);
    GVariant* streams = xdp_session_get_streams(a->session);
    GVariantIter it;
    g_variant_iter_init(&it, streams);
    guint32 node;
    GVariant* props;
    if (g_variant_iter_next(&it, "(u@a{sv})", &node, &props)) {
        a->node = node;
        g_variant_unref(props);
    }
    g_main_loop_quit(a->gloop);
}

static void on_created(GObject* src, GAsyncResult* res, gpointer data)
{
    auto* a = static_cast<App*>(data);
    GError* err = nullptr;
    a->session = xdp_portal_create_screencast_session_finish(XDP_PORTAL(src), res, &err);
    if (!a->session) {
        a->error = std::string("create session failed: ") + (err ? err->message : "?");
        g_main_loop_quit(a->gloop);
        return;
    }
    xdp_session_start(a->session, nullptr, nullptr, on_started, a);
}

// ---------------- vulkan dma-buf import ----------------
static bool import_and_copy(App& a, int fd, uint32_t offset, uint32_t stride, uint64_t modifier, uint32_t w, uint32_t h,
                            std::vector<uint8_t>* out_rgba)
{
    auto& vk = a.vk;
    VkSubresourceLayout plane{};
    plane.offset = offset;
    plane.rowPitch = stride;
    VkImageDrmFormatModifierExplicitCreateInfoEXT mi{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
    mi.drmFormatModifier = modifier;
    mi.drmFormatModifierPlaneCount = 1;
    mi.pPlaneLayouts = &plane;
    VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, &mi};
    ei.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ei};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_B8G8R8A8_UNORM;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img;
    if (vkCreateImage(vk.device, &ci, nullptr, &img) != VK_SUCCESS) return false;
    auto getFdProps = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdPropertiesKHR"));
    VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    int dfd = dup(fd);
    if (getFdProps(vk.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dfd, &fp) != VK_SUCCESS) {
        close(dfd);
        vkDestroyImage(vk.device, img, nullptr);
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, img, &req);
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    ded.image = img;
    VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &ded};
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    imp.fd = dfd;  // ownership passes to Vulkan on success
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp};
    ai.allocationSize = req.size;
    uint32_t bits = req.memoryTypeBits & fp.memoryTypeBits;
    ai.memoryTypeIndex = 0;
    while (ai.memoryTypeIndex < 32 && !(bits & (1u << ai.memoryTypeIndex))) ++ai.memoryTypeIndex;
    VkDeviceMemory mem;
    if (ai.memoryTypeIndex >= 32 || vkAllocateMemory(vk.device, &ai, nullptr, &mem) != VK_SUCCESS) {
        close(dfd);
        vkDestroyImage(vk.device, img, nullptr);
        return false;
    }
    VK_CHECK(vkBindImageMemory(vk.device, img, mem, 0));
    if (out_rgba) {
        Buffer hb = make_host_buffer(vk, VkDeviceSize(w) * h * 4);
        auto cb = vk.begin();
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;  // acquire from the compositor
        b.dstQueueFamilyIndex = vk.queue_family;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hb.buf, 1, &c);
        vk.submit_wait(cb);
        out_rgba->resize(size_t(w) * h * 4);
        auto* p = static_cast<uint8_t*>(hb.map);
        for (size_t i = 0; i < size_t(w) * h; ++i) {  // BGRA -> RGBA
            (*out_rgba)[i * 4 + 0] = p[i * 4 + 2];
            (*out_rgba)[i * 4 + 1] = p[i * 4 + 1];
            (*out_rgba)[i * 4 + 2] = p[i * 4 + 0];
            (*out_rgba)[i * 4 + 3] = 255;
        }
        destroy(vk, hb);
    }
    vkDestroyImage(vk.device, img, nullptr);
    vkFreeMemory(vk.device, mem, nullptr);
    return true;
}

// ---------------- pipewire ----------------
static const spa_pod* build_format(spa_pod_builder* b, spa_video_format f, const std::vector<uint64_t>* mods)
{
    spa_pod_frame fr[2];
    spa_rectangle def_sz = SPA_RECTANGLE(1920, 1080), min_sz = SPA_RECTANGLE(1, 1), max_sz = SPA_RECTANGLE(8192, 8192);
    spa_fraction def_fr = SPA_FRACTION(60, 1), min_fr = SPA_FRACTION(0, 1), max_fr = SPA_FRACTION(1000, 1);
    spa_pod_builder_push_object(b, &fr[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(f), 0);
    if (mods && mods->size() == 1) {
        // Already fixated by us: announce exactly this modifier (no choice, no DONT_FIXATE).
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(b, int64_t((*mods)[0]));
    } else if (mods && !mods->empty()) {
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &fr[1], SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(b, int64_t((*mods)[0]));
        for (uint64_t m : *mods) spa_pod_builder_long(b, int64_t(m));
        spa_pod_builder_pop(b, &fr[1]);
    }
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def_sz, &min_sz, &max_sz), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&def_fr, &min_fr, &max_fr), 0);
    return static_cast<const spa_pod*>(spa_pod_builder_pop(b, &fr[0]));
}

static void on_param_changed(void* data, uint32_t id, const spa_pod* param)
{
    auto* a = static_cast<App*>(data);
    if (!param || id != SPA_PARAM_Format) return;
    spa_video_info info{};
    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0) return;
    if (spa_format_video_raw_parse(param, &info.info.raw) < 0) return;
    a->fmt = info.info.raw;
    a->have_mod = (a->fmt.flags & SPA_VIDEO_FLAG_MODIFIER) != 0;

    // With DONT_FIXATE the consumer must fixate the modifier itself: pick the first one offered and
    // re-announce a single-modifier format (PipeWire's documented DMA-BUF negotiation).
    if (a->have_mod && (a->fmt.flags & SPA_VIDEO_FLAG_MODIFIER_FIXATION_REQUIRED)) {
        const spa_pod_prop* prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier);
        uint32_t n = 0, choice = 0;
        const spa_pod* val = spa_pod_get_values(&prop->value, &n, &choice);
        const int64_t* m = static_cast<const int64_t*>(SPA_POD_BODY(val));
        uint64_t fixed = n > 1 ? uint64_t(m[1]) : uint64_t(m[0]);
        std::printf("fixating modifier 0x%016llx (of %u offered)\n", (unsigned long long)fixed, n > 1 ? n - 1 : n);
        uint8_t buf[2048];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        std::vector<uint64_t> one{fixed};
        const spa_pod* params[2];
        params[0] = build_format(&b, a->fmt.format, &one);
        params[1] = build_format(&b, a->fmt.format, nullptr);
        pw_stream_update_params(a->stream, params, 2);
        return;
    }
    std::printf("negotiated: %s %ux%u @ %u/%u, modifier %s0x%016llx\n",
                spa_debug_type_find_short_name(spa_type_video_format, a->fmt.format), a->fmt.size.width,
                a->fmt.size.height, a->fmt.framerate.num, a->fmt.framerate.denom, a->have_mod ? "" : "(none) ",
                (unsigned long long)a->fmt.modifier);
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    int types = a->have_mod ? (1 << SPA_DATA_DmaBuf) : ((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr));
    const spa_pod* bp = static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(types)));
    pw_stream_update_params(a->stream, &bp, 1);
}

static void on_process(void* data)
{
    auto* a = static_cast<App*>(data);
    pw_buffer* pb = pw_stream_dequeue_buffer(a->stream);
    if (!pb) return;
    spa_data& d = pb->buffer->datas[0];
    auto now = std::chrono::steady_clock::now();
    if (a->frames == 0) a->t_first = now;
    a->span_ms = std::chrono::duration<double, std::milli>(now - a->t_first).count();
    ++a->frames;
    uint32_t w = a->fmt.size.width, h = a->fmt.size.height;
    if (d.type == SPA_DATA_DmaBuf) {
        ++a->dmabuf_frames;
        std::vector<uint8_t> rgba;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = import_and_copy(*a, int(d.fd), d.chunk->offset, uint32_t(d.chunk->stride), a->fmt.modifier, w, h,
                                  a->saved ? nullptr : &rgba);
        auto t1 = std::chrono::steady_clock::now();
        if (ok) {
            ++a->imported;
            if (a->saved) a->import_ms_sum += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
            ++a->import_fail;
        }
        if (ok && !a->saved) {
            write_ppm_rgba(a->out + "/frame_dmabuf.ppm", rgba.data(), w, h);
            a->saved = true;
        }
    } else if (d.data) {
        ++a->shm_frames;
        if (!a->saved) {
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            auto* p = static_cast<uint8_t*>(d.data) + d.chunk->offset;
            for (uint32_t j = 0; j < h; ++j)
                for (uint32_t i = 0; i < w; ++i) {
                    const uint8_t* s = p + size_t(j) * uint32_t(d.chunk->stride) + i * 4;
                    uint8_t* o = &rgba[(size_t(j) * w + i) * 4];
                    o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255;
                }
            write_ppm_rgba(a->out + "/frame_shm.ppm", rgba.data(), w, h);
            a->saved = true;
        }
    }
    pw_stream_queue_buffer(a->stream, pb);
    if (a->frames >= a->want_frames) pw_main_loop_quit(a->loop);
}

static const pw_stream_events kEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.param_changed = on_param_changed;
    e.process = on_process;
    return e;
}();

int main(int argc, char** argv)
{
    App a;
    a.out = argc > 1 ? argv[1] : ".";
    a.want_frames = argc > 2 ? std::stoi(argv[2]) : 60;
    int timeout_s = argc > 3 ? std::stoi(argv[3]) : 60;

    // Vulkan with DMA-BUF import support; collect importable modifiers for B8G8R8A8.
    a.vk.create_instance({});
    a.vk.pick_nvidia();
    a.vk.create_device({VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME});
    {
        VkDrmFormatModifierPropertiesListEXT ml{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 fp{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &ml};
        vkGetPhysicalDeviceFormatProperties2(a.vk.phys, VK_FORMAT_B8G8R8A8_UNORM, &fp);
        std::vector<VkDrmFormatModifierPropertiesEXT> props(ml.drmFormatModifierCount);
        ml.pDrmFormatModifierProperties = props.data();
        vkGetPhysicalDeviceFormatProperties2(a.vk.phys, VK_FORMAT_B8G8R8A8_UNORM, &fp);
        for (auto& p : props)
            if (p.drmFormatModifierPlaneCount == 1 && (p.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
                a.mods_bgra.push_back(p.drmFormatModifier);
        std::printf("Vulkan importable B8G8R8A8 modifiers (single plane, transfer-src): %zu\n", a.mods_bgra.size());
        for (uint64_t m : a.mods_bgra) std::printf("  0x%016llx\n", (unsigned long long)m);
    }

    // Portal.
    std::string token;
    std::ifstream(a.out + "/restore_token.txt") >> token;
    std::printf("portal: %s\n", token.empty() ? "no restore token, KDE will show a permission dialog"
                                              : "using saved restore token (no dialog expected)");
    std::fflush(stdout);
    XdpPortal* portal = xdp_portal_new();
    a.gloop = g_main_loop_new(nullptr, FALSE);
    xdp_portal_create_screencast_session(portal, XDP_OUTPUT_MONITOR, XDP_SCREENCAST_FLAG_NONE, XDP_CURSOR_MODE_EMBEDDED,
                                         XDP_PERSIST_MODE_PERSISTENT, token.empty() ? nullptr : token.c_str(), nullptr,
                                         on_created, &a);
    g_timeout_add_seconds(guint(timeout_s), [](gpointer d) -> gboolean {
        auto* app = static_cast<App*>(d);
        if (app->pw_fd < 0 && app->error.empty()) app->error = "timed out waiting for the portal (dialog not answered?)";
        g_main_loop_quit(app->gloop);
        return G_SOURCE_REMOVE;
    }, &a);
    g_main_loop_run(a.gloop);
    if (a.pw_fd < 0) {
        std::printf("RESULT portal=fail error=\"%s\"\n", a.error.c_str());
        return 3;
    }
    std::printf("portal: ok, PipeWire node %u\n", a.node);

    // PipeWire.
    pw_init(nullptr, nullptr);
    a.loop = pw_main_loop_new(nullptr);
    pw_context* ctx = pw_context_new(pw_main_loop_get_loop(a.loop), nullptr, 0);
    pw_core* core = pw_context_connect_fd(ctx, a.pw_fd, nullptr, 0);
    a.stream = pw_stream_new(core, "nitid-s6", pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                                                 "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    pw_stream_add_listener(a.stream, &a.listener, &kEvents, &a);
    uint8_t buf[8192];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    std::vector<const spa_pod*> params;
    for (auto f : {SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA}) params.push_back(build_format(&b, f, &a.mods_bgra));
    for (auto f : {SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA}) params.push_back(build_format(&b, f, nullptr));
    pw_stream_connect(a.stream, PW_DIRECTION_INPUT, a.node,
                      pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params.data(),
                      uint32_t(params.size()));
    spa_source* timer = pw_loop_add_timer(pw_main_loop_get_loop(a.loop),
                                          [](void* d, uint64_t) { pw_main_loop_quit(static_cast<App*>(d)->loop); }, &a);
    timespec ts{10, 0};
    pw_loop_update_timer(pw_main_loop_get_loop(a.loop), timer, &ts, nullptr, false);
    pw_main_loop_run(a.loop);

    double fps = a.frames > 1 && a.span_ms > 0 ? (a.frames - 1) * 1000.0 / a.span_ms : 0;
    std::printf("RESULT portal=ok frames=%d dmabuf=%d shm=%d imported=%d import_fail=%d fps=%.1f "
                "import_copy_ms_mean=%.3f modifier=0x%016llx size=%ux%u saved=%s\n",
                a.frames, a.dmabuf_frames, a.shm_frames, a.imported, a.import_fail, fps,
                a.imported > 1 ? a.import_ms_sum / (a.imported - 1) : 0.0, (unsigned long long)a.fmt.modifier,
                a.fmt.size.width, a.fmt.size.height, a.saved ? "yes" : "no");
    pw_stream_destroy(a.stream);
    pw_core_disconnect(core);
    pw_context_destroy(ctx);
    pw_main_loop_destroy(a.loop);
    if (a.session) xdp_session_close(a.session);
    a.vk.destroy();
    return 0;
}
