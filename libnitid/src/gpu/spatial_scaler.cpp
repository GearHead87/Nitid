#include "gpu/spatial_scaler.hpp"

#include "core/check.hpp"
#include "core/log.hpp"

#include <NIS_Config.h>  // third_party/nis: NVScalerUpdateConfig + filter tables

#include <cmath>
#include <vector>

#if NITID_HAVE_KERNELS
#include "nitid_scale_ptx.h"  // generated: const char nitid_scale_ptx[]
#endif

namespace nitid::gpu {

namespace {
unsigned div_up(uint32_t a, uint32_t b) { return unsigned((a + b - 1) / b); }
} // namespace

SpatialScaler::SpatialScaler()
{
#if NITID_HAVE_KERNELS
    if (cuModuleLoadData(&module_, nitid_scale_ptx) != CUDA_SUCCESS) {
        log::warn("FSR / NIS upscaling unavailable (PTX load failed)");
        module_ = nullptr;
        return;
    }
    if (cuModuleGetFunction(&easu_, module_, "fsr_easu") != CUDA_SUCCESS ||
        cuModuleGetFunction(&rcas_, module_, "fsr_rcas") != CUDA_SUCCESS ||
        cuModuleGetFunction(&nis_, module_, "nis_scaler") != CUDA_SUCCESS) {
        easu_ = rcas_ = nis_ = nullptr;
        log::warn("FSR / NIS upscaling unavailable (kernels missing)");
    }
#endif
}

SpatialScaler::~SpatialScaler()
{
    if (module_) cuModuleUnload(module_);
}

void SpatialScaler::configure(SpatialKind kind, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, float sharpness)
{
    if (!available()) fail("FSR / NIS kernels are not available in this build");
    if (kind == kind_ && sw == sw_ && sh == sh_ && dw == dw_ && dh == dh_ && sharpness == sharpness_) return;
    kind_ = kind, sw_ = sw, sh_ = sh, dw_ = dw, dh_ = dh, sharpness_ = sharpness;

    if (kind == SpatialKind::Fsr1) {
        if (temp_.size() != size_t(dw) * dh * 4) temp_ = CudaBuffer(size_t(dw) * dh * 4);
        return;
    }
    NISConfig c{};
    if (!NVScalerUpdateConfig(c, sharpness, 0, 0, sw, sh, sw, sh, 0, 0, dw, dh, dw, dh))
        failf("NIS can't scale {}x{} to {}x{} (1x to 2x only)", sw, sh, dw, dh);
    nis_params_ = {c.kDetectRatio,      c.kDetectThres,  c.kMinContrastRatio,   c.kRatioNorm,     c.kContrastBoost,
                   c.kEps,              c.kSharpStartY,  c.kSharpScaleY,        c.kSharpStrengthMin, c.kSharpStrengthScale,
                   c.kSharpLimitMin,    c.kSharpLimitScale, c.kScaleX,          c.kScaleY,        int(sw),
                   int(sh),             int(dw),         int(dh)};
    if (!coefs_) {
        // NIS_Config.h stores 8 taps per phase (padded for texture loads); the filter uses 6.
        std::vector<float> host(2 * kPhaseCount * 6);
        for (size_t p = 0; p < kPhaseCount; ++p)
            for (size_t t = 0; t < 6; ++t) {
                host[p * 6 + t] = coef_scale[p][t];
                host[kPhaseCount * 6 + p * 6 + t] = coef_usm[p][t];
            }
        coefs_ = CudaBuffer(host.size() * sizeof(float));
        NITID_CU(cuMemcpyHtoD(coefs_.ptr(), host.data(), host.size() * sizeof(float)));
    }
}

void SpatialScaler::run(CUdeviceptr src, CUdeviceptr dst, CUstream stream)
{
    if (kind_ == SpatialKind::Fsr1) {
        // EASU at src/dst scale, then RCAS. RCAS strength = 2^-stops (FsrRcasCon); Nitid's 0..1 sharpness is used
        // as that strength directly (0.5 = 1 stop, 1 = strongest, 0 = none).
        float sx = float(sw_) / float(dw_), sy = float(sh_) / float(dh_);
        float ox = 0.5f * sx - 0.5f, oy = 0.5f * sy - 0.5f;
        int sw = int(sw_), sh = int(sh_), dw = int(dw_), dh = int(dh_);
        CUdeviceptr tmp = temp_.ptr();
        void* a1[] = {&src, &sw, &sh, &tmp, &dw, &dh, &sx, &sy, &ox, &oy};
        NITID_CU(cuLaunchKernel(easu_, div_up(dw_, 16), div_up(dh_, 16), 1, 16, 16, 1, 0, stream, a1, nullptr));
        float sharp = std::fmin(std::fmax(sharpness_, 0.0f), 1.0f);
        void* a2[] = {&tmp, &dst, &dw, &dh, &sharp};
        NITID_CU(cuLaunchKernel(rcas_, div_up(dw_, 16), div_up(dh_, 16), 1, 16, 16, 1, 0, stream, a2, nullptr));
        return;
    }
    CUdeviceptr cs = coefs_.ptr(), cu = coefs_.ptr() + kPhaseCount * 6 * sizeof(float);
    void* args[] = {&src, &dst, &cs, &cu, &nis_params_};
    NITID_CU(cuLaunchKernel(nis_, div_up(dw_, 32), div_up(dh_, 24), 1, 256, 1, 1, 0, stream, args, nullptr));
}

} // namespace nitid::gpu
