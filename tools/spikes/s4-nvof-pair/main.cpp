// S4: Does VK_NV_optical_flow (the GPU's optical-flow hardware) give sensible motion vectors fast?
//
// 1. Capabilities: grid sizes, formats, limits.
// 2. Accuracy: frame B = frame A shifted by a known (dx, dy); measured flow should equal it.
// 3. Usefulness on real frames: warp the reference frame with the measured flow and compare it
//    with the input frame (luma PSNR), versus no warping. Also writes a colour image of the flow.
// 4. Speed: per-execution GPU time at 1080p / 1440p / 2160p for each grid size and performance level.
//
// Flow semantics used everywhere below: for a pixel at p in the INPUT image, the vector f(p)
// points to where that content is in the REFERENCE image: R(p + f(p)) ~ I(p).
//
// Usage: s4-nvof-pair <a.ppm> <b.ppm> <out_dir> [label]

#include "ppm.hpp"
#include "vk_ctx.hpp"
#include "vk_image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace spike;

static PFN_vkCreateOpticalFlowSessionNV pCreate;
static PFN_vkDestroyOpticalFlowSessionNV pDestroy;
static PFN_vkBindOpticalFlowSessionImageNV pBind;
static PFN_vkCmdOpticalFlowExecuteNV pExec;

struct Flow {
    uint32_t w = 0, h = 0, grid = 1;
    std::vector<float> fx, fy;  // per grid cell, in pixels
};

static std::vector<uint8_t> luma(const std::vector<uint8_t>& rgba, size_t px)
{
    std::vector<uint8_t> y(px);
    for (size_t i = 0; i < px; ++i)
        y[i] = uint8_t(std::lround(0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2]));
    return y;
}

static std::vector<uint8_t> resize_nearest(const std::vector<uint8_t>& y, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh)
{
    std::vector<uint8_t> o(size_t(nw) * nh);
    for (uint32_t j = 0; j < nh; ++j)
        for (uint32_t i = 0; i < nw; ++i) o[size_t(j) * nw + i] = y[size_t(j * h / nh) * w + i * w / nw];
    return o;
}

struct OfRunner {
    VkCtx& vk;
    VkFormat in_fmt, flow_fmt;
    OfRunner(VkCtx& v, VkFormat a, VkFormat b) : vk(v), in_fmt(a), flow_fmt(b) {}

    // Runs optical flow input->reference. Returns flow; ms_out gets per-execution GPU times (reps).
    Flow run(const std::vector<uint8_t>& in, const std::vector<uint8_t>& ref, uint32_t w, uint32_t h,
             VkOpticalFlowGridSizeFlagsNV grid, VkOpticalFlowPerformanceLevelNV perf, int reps,
             std::vector<double>* ms_out)
    {
        uint32_t g = grid == VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV ? 1 : grid == VK_OPTICAL_FLOW_GRID_SIZE_2X2_BIT_NV ? 2
                   : grid == VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV ? 4 : 8;
        uint32_t fw = (w + g - 1) / g, fh = (h + g - 1) / g;
        VkOpticalFlowImageFormatInfoNV in_info{VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV};
        in_info.usage = VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV;
        VkOpticalFlowImageFormatInfoNV out_info{VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV};
        out_info.usage = VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV;
        Image ia = make_image(vk, w, h, in_fmt, 0, &in_info, true, false);
        Image ib = make_image(vk, w, h, in_fmt, 0, &in_info, true, false);
        Image fo = make_image(vk, fw, fh, flow_fmt, 0, &out_info, true, false);
        Buffer up = make_host_buffer(vk, VkDeviceSize(w) * h), down = make_host_buffer(vk, VkDeviceSize(fw) * fh * 4);

        auto cb = vk.begin();
        std::memcpy(up.map, in.data(), in.size());
        cmd_upload(cb, up, ia);
        vk.submit_wait(cb);
        cb = vk.begin();
        std::memcpy(up.map, ref.data(), ref.size());
        cmd_upload(cb, up, ib);
        to_layout(cb, fo, VK_IMAGE_LAYOUT_GENERAL);
        vk.submit_wait(cb);

        VkOpticalFlowSessionCreateInfoNV sci{VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_INFO_NV};
        sci.width = w;
        sci.height = h;
        sci.imageFormat = in_fmt;
        sci.flowVectorFormat = flow_fmt;
        sci.outputGridSize = grid;
        sci.performanceLevel = perf;
        VkOpticalFlowSessionNV sess;
        VK_CHECK(pCreate(vk.device, &sci, nullptr, &sess));
        VK_CHECK(pBind(vk.device, sess, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_INPUT_NV, ia.view, VK_IMAGE_LAYOUT_GENERAL));
        VK_CHECK(pBind(vk.device, sess, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_REFERENCE_NV, ib.view, VK_IMAGE_LAYOUT_GENERAL));
        VK_CHECK(pBind(vk.device, sess, VK_OPTICAL_FLOW_SESSION_BINDING_POINT_FLOW_VECTOR_NV, fo.view, VK_IMAGE_LAYOUT_GENERAL));

        VkQueryPool qp;
        VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qci.queryCount = 2 * uint32_t(reps);
        VK_CHECK(vkCreateQueryPool(vk.device, &qci, nullptr, &qp));
        // warm-up
        auto oc = vk.begin2();
        VkOpticalFlowExecuteInfoNV ei{VK_STRUCTURE_TYPE_OPTICAL_FLOW_EXECUTE_INFO_NV};
        ei.flags = VK_OPTICAL_FLOW_EXECUTE_DISABLE_TEMPORAL_HINTS_BIT_NV;
        pExec(oc, sess, &ei);
        vk.submit_wait2(oc);
        oc = vk.begin2();
        vkCmdResetQueryPool(oc, qp, 0, 2 * uint32_t(reps));
        for (int r = 0; r < reps; ++r) {
            vkCmdWriteTimestamp(oc, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 2 * r);
            pExec(oc, sess, &ei);
            vkCmdWriteTimestamp(oc, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 2 * r + 1);
        }
        vk.submit_wait2(oc);
        if (ms_out) {
            std::vector<uint64_t> ts(2 * reps);
            VK_CHECK(vkGetQueryPoolResults(vk.device, qp, 0, 2 * reps, ts.size() * 8, ts.data(), 8,
                                           VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            for (int r = 0; r < reps; ++r)
                ms_out->push_back(double(ts[2 * r + 1] - ts[2 * r]) * vk.props.limits.timestampPeriod / 1e6);
        }
        vkDestroyQueryPool(vk.device, qp, nullptr);

        cb = vk.begin();
        cmd_download(cb, fo, down);
        vk.submit_wait(cb);
        Flow f;
        f.w = fw;
        f.h = fh;
        f.grid = g;
        f.fx.resize(size_t(fw) * fh);
        f.fy.resize(size_t(fw) * fh);
        auto* p = static_cast<const int16_t*>(down.map);  // R16G16 signed fixed point, 5 fractional bits
        for (size_t i = 0; i < size_t(fw) * fh; ++i) {
            f.fx[i] = float(p[i * 2]) / 32.0f;
            f.fy[i] = float(p[i * 2 + 1]) / 32.0f;
        }
        pDestroy(vk.device, sess, nullptr);
        destroy(vk, ia);
        destroy(vk, ib);
        destroy(vk, fo);
        destroy(vk, up);
        destroy(vk, down);
        return f;
    }
};

static float sample(const std::vector<uint8_t>& y, uint32_t w, uint32_t h, float x, float yy)
{
    x = std::clamp(x, 0.0f, float(w - 1));
    yy = std::clamp(yy, 0.0f, float(h - 1));
    uint32_t x0 = uint32_t(x), y0 = uint32_t(yy), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float ax = x - float(x0), ay = yy - float(y0);
    float a = y[size_t(y0) * w + x0], b = y[size_t(y0) * w + x1], c = y[size_t(y1) * w + x0], d = y[size_t(y1) * w + x1];
    return (a * (1 - ax) + b * ax) * (1 - ay) + (c * (1 - ax) + d * ax) * ay;
}

// PSNR of I vs R warped by flow (R(p + f(p))), over an interior margin.
static double warp_psnr(const std::vector<uint8_t>& I, const std::vector<uint8_t>& R, uint32_t w, uint32_t h,
                        const Flow* f, uint32_t margin = 32)
{
    double se = 0;
    size_t n = 0;
    for (uint32_t j = margin; j < h - margin; ++j)
        for (uint32_t i = margin; i < w - margin; ++i) {
            float dx = 0, dy = 0;
            if (f) {
                size_t k = size_t(std::min(j / f->grid, f->h - 1)) * f->w + std::min(i / f->grid, f->w - 1);
                dx = f->fx[k];
                dy = f->fy[k];
            }
            double d = double(I[size_t(j) * w + i]) - sample(R, w, h, float(i) + dx, float(j) + dy);
            se += d * d;
            ++n;
        }
    double mse = se / double(n);
    return mse < 1e-10 ? 99 : 10 * std::log10(255.0 * 255.0 / mse);
}

static void write_flow_png(const std::string& path, const Flow& f, float max_mag)
{
    std::vector<uint8_t> rgba(size_t(f.w) * f.h * 4);
    for (size_t i = 0; i < size_t(f.w) * f.h; ++i) {
        float a = std::atan2(f.fy[i], f.fx[i]), m = std::min(1.0f, std::hypot(f.fx[i], f.fy[i]) / max_mag);
        float hh = (a + float(M_PI)) / (2 * float(M_PI)) * 6.0f;
        int k = int(hh) % 6;
        float fr = hh - std::floor(hh), q = 1 - fr;
        float r = 0, g = 0, b = 0;
        switch (k) {
            case 0: r = 1; g = fr; break;
            case 1: r = q; g = 1; break;
            case 2: g = 1; b = fr; break;
            case 3: g = q; b = 1; break;
            case 4: r = fr; b = 1; break;
            default: r = 1; b = q; break;
        }
        // saturation = magnitude, value = 1 (white = no motion)
        rgba[i * 4 + 0] = uint8_t(255 * (1 - m + m * r));
        rgba[i * 4 + 1] = uint8_t(255 * (1 - m + m * g));
        rgba[i * 4 + 2] = uint8_t(255 * (1 - m + m * b));
        rgba[i * 4 + 3] = 255;
    }
    write_ppm_rgba(path, rgba.data(), f.w, f.h);
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s a.ppm b.ppm out_dir [label]\n", argv[0]);
        return 2;
    }
    std::string out = argv[3], label = argc > 4 ? argv[4] : "pair";
    uint32_t w, h, w2, h2;
    auto A = read_ppm_rgba(argv[1], w, h);
    auto B = read_ppm_rgba(argv[2], w2, h2);
    auto ya = luma(A, size_t(w) * h), yb = luma(B, size_t(w) * h);

    VkCtx vk;
    vk.create_instance({});
    vk.pick_nvidia();
    auto getFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceOpticalFlowImageFormatsNV>(
        vkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceOpticalFlowImageFormatsNV"));
    VkPhysicalDeviceOpticalFlowPropertiesNV ofp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_PROPERTIES_NV};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &ofp};
    vkGetPhysicalDeviceProperties2(vk.phys, &p2);
    std::printf("## Capabilities\n\n- output grids: 0x%x, hint grids: 0x%x, hint %u, cost %u, bidirectional %u, global flow %u\n",
                ofp.supportedOutputGridSizes, ofp.supportedHintGridSizes, ofp.hintSupported, ofp.costSupported,
                ofp.bidirectionalFlowSupported, ofp.globalFlowSupported);
    std::printf("- size: %ux%u .. %ux%u, max regions %u\n", ofp.minWidth, ofp.minHeight, ofp.maxWidth, ofp.maxHeight,
                ofp.maxNumRegionsOfInterest);
    auto formats = [&](VkOpticalFlowUsageFlagsNV u) {
        VkOpticalFlowImageFormatInfoNV fi{VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV};
        fi.usage = u;
        uint32_t n = 0;
        getFormats(vk.phys, &fi, &n, nullptr);
        std::vector<VkOpticalFlowImageFormatPropertiesNV> v(n, {VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_NV});
        getFormats(vk.phys, &fi, &n, v.data());
        std::vector<VkFormat> r;
        for (auto& x : v) r.push_back(x.format);
        return r;
    };
    auto fin = formats(VK_OPTICAL_FLOW_USAGE_INPUT_BIT_NV), fout = formats(VK_OPTICAL_FLOW_USAGE_OUTPUT_BIT_NV);
    std::printf("- input formats:");
    for (auto f : fin) std::printf(" %d", f);
    std::printf("\n- flow formats:");
    for (auto f : fout) std::printf(" %d", f);
    std::printf("   (R8_UNORM=%d, R16G16_SFIXED5/S10_5=%d)\n", VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16_S10_5_NV);

    VkPhysicalDeviceOpticalFlowFeaturesNV off{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    vk.create_device({VK_NV_OPTICAL_FLOW_EXTENSION_NAME}, &off, VK_QUEUE_OPTICAL_FLOW_BIT_NV);
    std::printf("- feature opticalFlow = %u, OF queue family %u, main family %u\n", off.opticalFlow, vk.queue2_family,
                vk.queue_family);
    pCreate = reinterpret_cast<PFN_vkCreateOpticalFlowSessionNV>(vkGetDeviceProcAddr(vk.device, "vkCreateOpticalFlowSessionNV"));
    pDestroy = reinterpret_cast<PFN_vkDestroyOpticalFlowSessionNV>(vkGetDeviceProcAddr(vk.device, "vkDestroyOpticalFlowSessionNV"));
    pBind = reinterpret_cast<PFN_vkBindOpticalFlowSessionImageNV>(vkGetDeviceProcAddr(vk.device, "vkBindOpticalFlowSessionImageNV"));
    pExec = reinterpret_cast<PFN_vkCmdOpticalFlowExecuteNV>(vkGetDeviceProcAddr(vk.device, "vkCmdOpticalFlowExecuteNV"));
    OfRunner of(vk, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16_S10_5_NV);

    // 2. Accuracy with a known shift: B content = A content moved by (+dx, +dy).
    {
        const int dx = 12, dy = -5;
        std::vector<uint8_t> ys(ya.size());
        for (uint32_t j = 0; j < h; ++j)
            for (uint32_t i = 0; i < w; ++i) {
                int si = std::clamp(int(i) - dx, 0, int(w) - 1), sj = std::clamp(int(j) - dy, 0, int(h) - 1);
                ys[size_t(j) * w + i] = ya[size_t(sj) * w + si];
            }
        std::printf("\n## Known shift: reference = input moved by (%+d, %+d) px\n\n| Grid | Perf | Mean flow (x, y) | Median |error| px | Within 0.5 px |\n|---|---|---|---|---|\n", dx, dy);
        for (auto grid : {VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV, VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV}) {
            if (!(ofp.supportedOutputGridSizes & grid)) continue;
            auto f = of.run(ya, ys, w, h, grid, VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV, 1, nullptr);
            double sx = 0, sy = 0;
            std::vector<float> err;
            size_t ok = 0;
            uint32_t m = 48 / f.grid;
            for (uint32_t j = m; j < f.h - m; ++j)
                for (uint32_t i = m; i < f.w - m; ++i) {
                    size_t k = size_t(j) * f.w + i;
                    sx += f.fx[k];
                    sy += f.fy[k];
                    float e = std::hypot(f.fx[k] - dx, f.fy[k] - dy);
                    err.push_back(e);
                    ok += e <= 0.5f;
                }
            std::sort(err.begin(), err.end());
            std::printf("| %ux%u | SLOW | (%.2f, %.2f) | %.3f | %.1f%% |\n", f.grid, f.grid, sx / double(err.size()),
                        sy / double(err.size()), err[err.size() / 2], 100.0 * double(ok) / double(err.size()));
        }
    }

    // 3. Real pair: warp reference onto input.
    {
        std::printf("\n## Real pair (%s, %ux%u): reference warped by measured flow vs input\n\n| Method | Luma PSNR (dB) |\n|---|---|\n",
                    label.c_str(), w, h);
        std::printf("| no warp (reference as-is) | %.2f |\n", warp_psnr(ya, yb, w, h, nullptr));
        for (auto grid : {VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV, VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV}) {
            if (!(ofp.supportedOutputGridSizes & grid)) continue;
            for (auto perf : {VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV, VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_FAST_NV}) {
                auto f = of.run(ya, yb, w, h, grid, perf, 1, nullptr);
                std::printf("| NVOF %ux%u %s | %.2f |\n", f.grid, f.grid, perf == VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV ? "SLOW" : "FAST",
                            warp_psnr(ya, yb, w, h, &f));
                if (grid == VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV && perf == VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV) {
                    float mx = 1;
                    for (size_t i = 0; i < f.fx.size(); ++i) mx = std::max(mx, std::hypot(f.fx[i], f.fy[i]));
                    write_flow_png(out + "/flow_" + label + ".ppm", f, std::min(mx, 64.0f));
                    std::printf("\n(max |flow| = %.1f px; colour image: flow_%s.ppm, white = no motion)\n\n| Method | Luma PSNR (dB) |\n|---|---|\n", mx, label.c_str());
                }
            }
        }
    }

    // 4. Speed.
    std::printf("\n## Speed (GPU ms per execution, median of 20, on the optical-flow queue)\n\n| Resolution | Grid | SLOW | MEDIUM | FAST |\n|---|---|---|---|---|\n");
    for (auto [rw, rh] : {std::pair{1920u, 1080u}, std::pair{2560u, 1440u}, std::pair{3840u, 2160u}}) {
        auto ra = resize_nearest(ya, w, h, rw, rh), rb = resize_nearest(yb, w, h, rw, rh);
        for (auto grid : {VK_OPTICAL_FLOW_GRID_SIZE_1X1_BIT_NV, VK_OPTICAL_FLOW_GRID_SIZE_2X2_BIT_NV, VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV}) {
            if (!(ofp.supportedOutputGridSizes & grid)) continue;
            std::printf("| %ux%u | %u | ", rw, rh, grid == 1 ? 1 : grid == 2 ? 2 : 4);
            for (auto perf : {VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV, VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_NV,
                              VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_FAST_NV}) {
                std::vector<double> ms;
                of.run(ra, rb, rw, rh, grid, perf, 20, &ms);
                std::sort(ms.begin(), ms.end());
                std::printf("%.3f | ", ms[ms.size() / 2]);
            }
            std::printf("\n");
        }
    }
    vk.destroy();
    return 0;
}
