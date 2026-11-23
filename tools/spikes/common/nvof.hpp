// VK_NV_optical_flow helper shared by S4 and F1 (moved from s4-nvof-pair/main.cpp).
// Flow semantics: for a pixel p of the INPUT image, REFERENCE content is at p + f(p).
#pragma once

#include "vk_ctx.hpp"
#include "vk_image.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace spike {

inline PFN_vkCreateOpticalFlowSessionNV pCreate;
inline PFN_vkDestroyOpticalFlowSessionNV pDestroy;
inline PFN_vkBindOpticalFlowSessionImageNV pBind;
inline PFN_vkCmdOpticalFlowExecuteNV pExec;

struct Flow {
    uint32_t w = 0, h = 0, grid = 1;
    std::vector<float> fx, fy;  // per grid cell, in pixels
};

inline std::vector<uint8_t> luma(const std::vector<uint8_t>& rgba, size_t px)
{
    std::vector<uint8_t> y(px);
    for (size_t i = 0; i < px; ++i)
        y[i] = uint8_t(std::lround(0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2]));
    return y;
}

inline std::vector<uint8_t> resize_nearest(const std::vector<uint8_t>& y, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh)
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


inline void nvof_load_functions(VkDevice d)
{
    pCreate = reinterpret_cast<PFN_vkCreateOpticalFlowSessionNV>(vkGetDeviceProcAddr(d, "vkCreateOpticalFlowSessionNV"));
    pDestroy = reinterpret_cast<PFN_vkDestroyOpticalFlowSessionNV>(vkGetDeviceProcAddr(d, "vkDestroyOpticalFlowSessionNV"));
    pBind = reinterpret_cast<PFN_vkBindOpticalFlowSessionImageNV>(vkGetDeviceProcAddr(d, "vkBindOpticalFlowSessionImageNV"));
    pExec = reinterpret_cast<PFN_vkCmdOpticalFlowExecuteNV>(vkGetDeviceProcAddr(d, "vkCmdOpticalFlowExecuteNV"));
}

}  // namespace spike
