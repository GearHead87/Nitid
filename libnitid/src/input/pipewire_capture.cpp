#include "input/pipewire_capture.hpp"

#include "core/check.hpp"
#include "core/log.hpp"
#include "core/running_average.hpp"
#include "gpu/interop.hpp"
#include "gpu/vk_check.hpp"
#include "platform/pipewire_lib.hpp"

#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <format>
#include <unordered_map>
#include <vector>

namespace nitid::input {

namespace {

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

const char* format_name(uint32_t f)
{
    switch (f) {
        case SPA_VIDEO_FORMAT_BGRx: return "BGRx";
        case SPA_VIDEO_FORMAT_BGRA: return "BGRA";
        case SPA_VIDEO_FORMAT_RGBx: return "RGBx";
        case SPA_VIDEO_FORMAT_RGBA: return "RGBA";
        default: return "?";
    }
}

// Modifiers Vulkan can import for `format` as a single-plane image usable as a copy source (spike S6).
std::vector<uint64_t> importable_modifiers(VkPhysicalDevice phys, VkFormat format)
{
    VkDrmFormatModifierPropertiesListEXT ml{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
    VkFormatProperties2 fp{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &ml};
    vkGetPhysicalDeviceFormatProperties2(phys, format, &fp);
    std::vector<VkDrmFormatModifierPropertiesEXT> props(ml.drmFormatModifierCount);
    ml.pDrmFormatModifierProperties = props.data();
    vkGetPhysicalDeviceFormatProperties2(phys, format, &fp);
    std::vector<uint64_t> out;
    for (auto& p : props)
        if (p.drmFormatModifierPlaneCount == 1 && (p.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
            out.push_back(p.drmFormatModifier);
    return out;
}

const spa_pod* build_format(spa_pod_builder* b, spa_video_format f, const std::vector<uint64_t>* mods)
{
    spa_pod_frame fr[2];
    spa_rectangle def_sz = SPA_RECTANGLE(1920, 1080), min_sz = SPA_RECTANGLE(1, 1), max_sz = SPA_RECTANGLE(8192, 8192);
    spa_fraction def_fr = SPA_FRACTION(60, 1), min_fr = SPA_FRACTION(0, 1), max_fr = SPA_FRACTION(1000, 1);
    spa_pod_builder_push_object(b, &fr[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(f), 0);
    if (mods && mods->size() == 1) {
        // Fixated by us: exactly this modifier (announcing it as a choice makes PipeWire ask again; spike S6).
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

} // namespace

struct PipeWireCapture::Impl {
    gpu::VulkanContext& vk;
    gpu::CudaContext& cuda;
    gpu::Kernels& kernels;
    const platform::PipeWireLib* pw = nullptr;
    Connect connect;
    std::thread setup;
    std::atomic<bool> stop{false};

    // PipeWire
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook listener{};
    spa_video_info_raw fmt{};
    bool has_modifier = false;
    std::vector<uint64_t> mods_bgr, mods_rgb;

    // GPU (PipeWire thread)
    CUstream cstream = nullptr;
    gpu::CudaBuffer staging;  // shared-memory frames, uploaded with their row pitch
    std::unique_ptr<gpu::SharedBuffer> linear;  // DMA-BUF frames copied here by Vulkan (BGRx, tight)
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    struct Imported {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };
    std::unordered_map<int, Imported> imports;  // by DMA-BUF fd (PipeWire recycles a few buffers)
    PFN_vkGetMemoryFdPropertiesKHR get_fd_props = nullptr;

    // Shared with the player's thread
    mutable std::mutex m;
    std::condition_variable cv;
    std::optional<DecodedFrame> latest;
    std::vector<gpu::CudaBuffer> free_buffers;
    uint64_t frames = 0, dropped = 0;
    int64_t next_index = 0;
    double last_arrival = 0, interval_ema = 0;
    std::string status = "connecting";
    bool failed = false;
    std::string name = "PipeWire";
    uint32_t width = 0, height = 0;
    std::string path;  // "DMA-BUF" or "shared memory"

    Impl(gpu::VulkanContext& v, gpu::CudaContext& c, gpu::Kernels& k) : vk(v), cuda(c), kernels(k) {}

    void set_status(std::string s, bool fail = false)
    {
        std::lock_guard l(m);
        if (fail) log::error("capture: {}", s);
        else log::info("capture: {}", s);
        status = std::move(s);
        failed = fail;
        cv.notify_all();
    }

    // --- setup (own thread) --------------------------------------------------------------------------------
    void run_setup()
    {
        std::string err;
        pw = platform::pipewire(&err);
        if (!pw) return set_status(err, true);
        Target t;
        set_status("connecting");
        if (!connect(t, err, stop, [this](std::string st) { set_status(std::move(st)); }))
            return set_status(err.empty() ? "cancelled" : err, true);
        if (stop) return;
        {
            std::lock_guard l(m);
            if (!t.name.empty()) name = t.name;
        }
        if (vk.dma_buf_import && vk.capture_queue != vk.queue) {
            mods_bgr = importable_modifiers(vk.phys, VK_FORMAT_B8G8R8A8_UNORM);
            mods_rgb = importable_modifiers(vk.phys, VK_FORMAT_R8G8B8A8_UNORM);
        }
        loop = pw->thread_loop_new("nitid-capture", nullptr);
        pw->thread_loop_lock(loop);
        context = pw->context_new(pw->thread_loop_get_loop(loop), nullptr, 0);
        core = t.fd >= 0 ? pw->context_connect_fd(context, t.fd, nullptr, 0) : pw->context_connect(context, nullptr, 0);
        if (!core) {
            pw->thread_loop_unlock(loop);
            return set_status("can't connect to PipeWire", true);
        }
        // A target node name / serial is only passed when given (a null key ends the list).
        pw_properties* props = pw->properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  PW_KEY_MEDIA_ROLE, "Screen", PW_KEY_APP_NAME, "Nitid",
                                                  t.target.empty() ? nullptr : PW_KEY_TARGET_OBJECT, t.target.c_str(),
                                                  nullptr);
        stream = pw->stream_new(core, "Nitid capture", props);
        static const pw_stream_events events = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = &Impl::on_state;
            e.param_changed = &Impl::on_param;
            e.process = &Impl::on_process;
            return e;
        }();
        pw->stream_add_listener(stream, &listener, &events, this);
        uint8_t buf[8192];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        std::vector<const spa_pod*> params;
        // DMA-BUF first (zero-copy from the compositor), then shared memory.
        if (!mods_bgr.empty()) {
            params.push_back(build_format(&b, SPA_VIDEO_FORMAT_BGRx, &mods_bgr));
            params.push_back(build_format(&b, SPA_VIDEO_FORMAT_BGRA, &mods_bgr));
        }
        if (!mods_rgb.empty()) {
            params.push_back(build_format(&b, SPA_VIDEO_FORMAT_RGBx, &mods_rgb));
            params.push_back(build_format(&b, SPA_VIDEO_FORMAT_RGBA, &mods_rgb));
        }
        for (auto f : {SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA})
            params.push_back(build_format(&b, f, nullptr));
        int r = pw->stream_connect(stream, PW_DIRECTION_INPUT, t.node,
                                   pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params.data(),
                                   uint32_t(params.size()));
        pw->thread_loop_unlock(loop);
        if (r < 0) return set_status("PipeWire stream failed to connect", true);
        pw->thread_loop_start(loop);
        set_status("waiting for the first frame");
    }

    // --- PipeWire callbacks (PipeWire thread) ---------------------------------------------------------------
    static void on_state(void* d, pw_stream_state, pw_stream_state state, const char* error)
    {
        auto* self = static_cast<Impl*>(d);
        if (state == PW_STREAM_STATE_ERROR) self->set_status(std::string("PipeWire: ") + (error ? error : "error"), true);
        if (state == PW_STREAM_STATE_UNCONNECTED && !self->stop) self->set_status("the capture ended (stream closed)", true);
    }

    static void on_param(void* d, uint32_t id, const spa_pod* param)
    {
        auto* self = static_cast<Impl*>(d);
        if (!param || id != SPA_PARAM_Format) return;
        uint32_t mt = 0, ms = 0;
        if (spa_format_parse(param, &mt, &ms) < 0 || mt != SPA_MEDIA_TYPE_video || ms != SPA_MEDIA_SUBTYPE_raw) return;
        spa_video_info_raw f{};
        if (spa_format_video_raw_parse(param, &f) < 0) return;
        self->fmt = f;
        self->has_modifier = (f.flags & SPA_VIDEO_FLAG_MODIFIER) != 0;
        self->clear_imports();
        if (self->has_modifier && (f.flags & SPA_VIDEO_FLAG_MODIFIER_FIXATION_REQUIRED)) {
            // Pick a modifier ourselves and announce it alone (PipeWire's DMA-BUF negotiation, spike S6).
            const spa_pod_prop* prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier);
            if (!prop) return;
            uint32_t n = 0, choice = 0;
            const spa_pod* val = spa_pod_get_values(&prop->value, &n, &choice);
            const auto* mv = static_cast<const int64_t*>(SPA_POD_BODY(val));
            std::vector<uint64_t> one{n > 1 ? uint64_t(mv[1]) : uint64_t(mv[0])};
            uint8_t buf[2048];
            spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
            const spa_pod* params[2] = {build_format(&b, spa_video_format(f.format), &one),
                                        build_format(&b, spa_video_format(f.format), nullptr)};
            self->pw->stream_update_params(self->stream, params, 2);
            return;
        }
        uint8_t buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        int types = self->has_modifier ? (1 << SPA_DATA_DmaBuf) : ((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr));
        const spa_pod* bp = static_cast<const spa_pod*>(spa_pod_builder_add_object(
            &b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(types)));
        self->pw->stream_update_params(self->stream, &bp, 1);
        {
            std::lock_guard l(self->m);
            self->width = f.size.width, self->height = f.size.height;
            self->path = self->has_modifier ? "DMA-BUF" : "shared memory";
        }
        log::info("capture: {} {}x{}, {}{}", format_name(f.format), f.size.width, f.size.height,
                  self->has_modifier ? "DMA-BUF modifier " : "shared memory",
                  self->has_modifier ? std::format("{:#018x}", f.modifier) : "");
    }

    static void on_process(void* d) { static_cast<Impl*>(d)->process(); }

    void process()
    {
        pw_buffer* b = pw->stream_dequeue_buffer(stream);
        if (!b) return;
        const spa_data& d = b->buffer->datas[0];
        const uint32_t w = fmt.size.width, h = fmt.size.height;
        const bool bgr = fmt.format == SPA_VIDEO_FORMAT_BGRx || fmt.format == SPA_VIDEO_FORMAT_BGRA;
        const bool corrupted = d.chunk && (d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED);
        const bool empty = d.type != SPA_DATA_DmaBuf && (!d.data || !d.chunk || d.chunk->size == 0);
        if (w && h && !corrupted && !empty) {
            try {
                convert(d, w, h, bgr);
            } catch (const std::exception& e) {
                log::error("capture frame: {}", e.what());
            }
        }
        pw->stream_queue_buffer(stream, b);
    }

    void convert(const spa_data& d, uint32_t w, uint32_t h, bool bgr)
    {
        auto t0 = SteadyClock::now();
        cuCtxSetCurrent(cuda.ctx);
        if (!cstream) NITID_CU(cuStreamCreate(&cstream, CU_STREAM_NON_BLOCKING));
        DecodedFrame f;
        const size_t bytes = size_t(w) * h * 4;
        {
            std::lock_guard l(m);
            while (!free_buffers.empty() && !f.gpu_rgba) {
                if (free_buffers.back().size() == bytes) f.gpu_rgba = std::move(free_buffers.back());
                free_buffers.pop_back();
            }
        }
        if (!f.gpu_rgba) f.gpu_rgba = gpu::CudaBuffer(bytes);
        const size_t pitch = d.chunk && d.chunk->stride > 0 ? size_t(d.chunk->stride) : size_t(w) * 4;
        if (d.type == SPA_DATA_DmaBuf) {
            copy_dmabuf(int(d.fd), d.chunk ? d.chunk->offset : 0, uint32_t(pitch), w, h);
            kernels.rgbx_to_rgba(linear->ptr(), size_t(w) * 4, f.gpu_rgba.ptr(), int(w), int(h), bgr, cstream);
        } else {
            if (staging.size() < pitch * h) staging = gpu::CudaBuffer(pitch * h);
            const auto* src = static_cast<const uint8_t*>(d.data) + (d.chunk ? d.chunk->offset : 0);
            NITID_CU(cuMemcpyHtoDAsync(staging.ptr(), src, pitch * h, cstream));
            kernels.rgbx_to_rgba(staging.ptr(), pitch, f.gpu_rgba.ptr(), int(w), int(h), bgr, cstream);
        }
        NITID_CU(cuStreamSynchronize(cstream));
        f.width = w, f.height = h;
        f.decode_ms = ms_since(t0);
        f.arrival_s = now_s();
        std::lock_guard l(m);
        f.index = next_index++;
        f.time_s = f.arrival_s;
        if (last_arrival > 0) {
            double dt = f.arrival_s - last_arrival;
            interval_ema = interval_ema > 0 ? interval_ema + (dt - interval_ema) * 0.1 : dt;
        }
        last_arrival = f.arrival_s;
        if (latest) {  // the player didn't take the previous one: drop it
            ++dropped;
            if (latest->gpu_rgba && free_buffers.size() < 6) free_buffers.push_back(std::move(latest->gpu_rgba));
        }
        latest = std::move(f);
        if (++frames == 1) status = std::format("capturing {}x{} ({})", w, h, path);
        cv.notify_all();
    }

    // Import the compositor's buffer (once per fd) and copy it into a linear buffer CUDA can read.
    void copy_dmabuf(int fd, uint32_t offset, uint32_t stride, uint32_t w, uint32_t h)
    {
        if (!pool) {
            VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pci.queueFamilyIndex = vk.queue_family;
            NITID_VK(vkCreateCommandPool(vk.device, &pci, nullptr, &pool));
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            NITID_VK(vkAllocateCommandBuffers(vk.device, &ai, &cmd));
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            NITID_VK(vkCreateFence(vk.device, &fci, nullptr, &fence));
            get_fd_props = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdPropertiesKHR"));
        }
        const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
        if (!linear || linear->size() != bytes) linear = std::make_unique<gpu::SharedBuffer>(vk, bytes);
        auto it = imports.find(fd);
        if (it == imports.end()) it = imports.emplace(fd, import(fd, offset, stride, w, h)).first;
        VkImage img = it->second.image;

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        NITID_VK(vkBeginCommandBuffer(cmd, &bi));
        VkImageMemoryBarrier acq{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        acq.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        acq.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        acq.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        acq.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;  // owned by the compositor until now
        acq.dstQueueFamilyIndex = vk.queue_family;
        acq.image = img;
        acq.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &acq);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, linear->buffer(), 1, &c);
        VkImageMemoryBarrier rel = acq;  // hand it back to the compositor
        rel.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        rel.dstAccessMask = 0;
        rel.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rel.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        rel.srcQueueFamilyIndex = vk.queue_family;
        rel.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &rel);
        NITID_VK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        NITID_VK(vkResetFences(vk.device, 1, &fence));
        NITID_VK(vkQueueSubmit(vk.capture_queue, 1, &si, fence));
        NITID_VK(vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX));
    }

    Imported import(int fd, uint32_t offset, uint32_t stride, uint32_t w, uint32_t h)
    {
        const bool bgr = fmt.format == SPA_VIDEO_FORMAT_BGRx || fmt.format == SPA_VIDEO_FORMAT_BGRA;
        VkSubresourceLayout plane{};
        plane.offset = offset;
        plane.rowPitch = stride;
        VkImageDrmFormatModifierExplicitCreateInfoEXT mi{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
        mi.drmFormatModifier = fmt.modifier;
        mi.drmFormatModifierPlaneCount = 1;
        mi.pPlaneLayouts = &plane;
        VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, &mi};
        ei.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ei};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = bgr ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {w, h, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        Imported im;
        NITID_VK(vkCreateImage(vk.device, &ci, nullptr, &im.image));
        int dfd = dup(fd);
        VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
        if (get_fd_props(vk.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dfd, &fp) != VK_SUCCESS) {
            close(dfd);
            vkDestroyImage(vk.device, im.image, nullptr);
            fail("can't read the captured buffer's memory properties");
        }
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(vk.device, im.image, &req);
        VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        ded.image = im.image;
        VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &ded};
        imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        imp.fd = dfd;  // Vulkan owns it on success
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp};
        ai.allocationSize = req.size;
        uint32_t bits = req.memoryTypeBits & fp.memoryTypeBits, type = 0;
        while (type < 32 && !(bits & (1u << type))) ++type;
        ai.memoryTypeIndex = type;
        if (type >= 32 || vkAllocateMemory(vk.device, &ai, nullptr, &im.memory) != VK_SUCCESS) {
            close(dfd);
            vkDestroyImage(vk.device, im.image, nullptr);
            fail("can't import the captured DMA-BUF into Vulkan");
        }
        NITID_VK(vkBindImageMemory(vk.device, im.image, im.memory, 0));
        return im;
    }

    void clear_imports()
    {
        if (!imports.empty() && fence) vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
        for (auto& [fd, im] : imports) {
            vkDestroyImage(vk.device, im.image, nullptr);
            vkFreeMemory(vk.device, im.memory, nullptr);
        }
        imports.clear();
    }

    void shutdown()
    {
        stop = true;
        cv.notify_all();
        if (setup.joinable()) setup.join();
        if (pw && loop) pw->thread_loop_stop(loop);
        if (pw && stream) pw->stream_destroy(stream);
        if (pw && core) pw->core_disconnect(core);
        if (pw && context) pw->context_destroy(context);
        if (pw && loop) pw->thread_loop_destroy(loop);
        stream = nullptr, core = nullptr, context = nullptr, loop = nullptr;
        cuCtxSetCurrent(cuda.ctx);
        clear_imports();
        if (fence) vkDestroyFence(vk.device, fence, nullptr);
        if (pool) vkDestroyCommandPool(vk.device, pool, nullptr);
        linear.reset();
        staging = {};
        latest.reset();
        free_buffers.clear();
        if (cstream) cuStreamDestroy(cstream);
        cstream = nullptr;
    }
};

PipeWireCapture::PipeWireCapture(Connect connect, gpu::VulkanContext& vk, gpu::CudaContext& cuda, gpu::Kernels& kernels)
    : impl_(std::make_unique<Impl>(vk, cuda, kernels))
{
    if (!kernels.available()) fail("screen capture needs Nitid's GPU kernels (built with nvcc)");
    impl_->connect = std::move(connect);
    info_.name = "PipeWire";
    info_.codec = "live";
    info_.decoder = "PipeWire";
    info_.fps = 60;
    impl_->setup = std::thread([this] { impl_->run_setup(); });
}

PipeWireCapture::~PipeWireCapture() { impl_->shutdown(); }

const SourceInfo& PipeWireCapture::info() const
{
    std::lock_guard l(impl_->m);
    info_.name = impl_->name;
    info_.width = impl_->width;
    info_.height = impl_->height;
    info_.fps = impl_->interval_ema > 0 ? 1.0 / impl_->interval_ema : 60;
    info_.decoder = impl_->path.empty() ? std::string("PipeWire") : "PipeWire, " + impl_->path;
    return info_;
}

std::optional<DecodedFrame> PipeWireCapture::try_next()
{
    std::lock_guard l(impl_->m);
    if (!impl_->latest) return std::nullopt;
    auto f = std::move(impl_->latest);
    impl_->latest.reset();
    return f;
}

std::optional<DecodedFrame> PipeWireCapture::next()
{
    std::unique_lock l(impl_->m);
    impl_->cv.wait_for(l, std::chrono::seconds(1), [&] { return impl_->latest || impl_->failed || impl_->stop; });
    if (!impl_->latest) return std::nullopt;
    auto f = std::move(impl_->latest);
    impl_->latest.reset();
    return f;
}

void PipeWireCapture::recycle(DecodedFrame&& frame)
{
    std::lock_guard l(impl_->m);
    if (frame.gpu_rgba && impl_->free_buffers.size() < 6) impl_->free_buffers.push_back(std::move(frame.gpu_rgba));
}

uint64_t PipeWireCapture::dropped() const
{
    std::lock_guard l(impl_->m);
    return impl_->dropped;
}

std::string PipeWireCapture::status() const
{
    std::lock_guard l(impl_->m);
    return impl_->status;
}

bool PipeWireCapture::failed() const
{
    std::lock_guard l(impl_->m);
    return impl_->failed;
}

} // namespace nitid::input
