// DLSS-G helpers shared by S3 and F1: luma PSNR and the stationary synthetic camera (Veyra's idea,
// written from scratch). Moved from s3-dlssg-standalone/main.cpp.
#pragma once

#include <nvsdk_ngx_helpers_dlssg_vk.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace spike {

inline double luma_psnr(const uint8_t* a, const uint8_t* b, size_t px)
{
    double se = 0;
    for (size_t i = 0; i < px; ++i) {
        double ya = 0.299 * a[i * 4] + 0.587 * a[i * 4 + 1] + 0.114 * a[i * 4 + 2];
        double yb = 0.299 * b[i * 4] + 0.587 * b[i * 4 + 1] + 0.114 * b[i * 4 + 2];
        se += (ya - yb) * (ya - yb);
    }
    double mse = se / double(px);
    return mse <= 1e-10 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

inline void synthetic_camera(NVSDK_NGX_DLSSG_Opt_Eval_Params& o, uint32_t w, uint32_t h)
{
    // Stationary pinhole camera: all motion lives in the motion-vector buffer.
    auto ident = [](float m[4][4]) {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) m[r][c] = r == c ? 1.0f : 0.0f;
    };
    ident(o.cameraViewToClip);
    ident(o.clipToCameraView);
    ident(o.clipToLensClip);
    ident(o.clipToPrevClip);
    ident(o.prevClipToClip);
    const float n = 0.1f, f = 1000.0f, aspect = float(w) / float(h);
    const float fov = 1.5707963f;  // 90 degrees vertical: tan(fov/2) = 1
    // Row-major, post-multiplication (row vector * matrix), D3D-style depth [0,1].
    const float sx = 1.0f / aspect, sy = 1.0f, a = f / (f - n), b = -n * f / (f - n);
    o.cameraViewToClip[0][0] = sx;
    o.cameraViewToClip[1][1] = sy;
    o.cameraViewToClip[2][2] = a;
    o.cameraViewToClip[2][3] = 1.0f;
    o.cameraViewToClip[3][2] = b;
    o.cameraViewToClip[3][3] = 0.0f;
    // Inverse of the above.
    o.clipToCameraView[0][0] = 1.0f / sx;
    o.clipToCameraView[1][1] = 1.0f / sy;
    o.clipToCameraView[2][2] = 0.0f;
    o.clipToCameraView[2][3] = 1.0f / b;
    o.clipToCameraView[3][2] = 1.0f;
    o.clipToCameraView[3][3] = -a / b;
    o.cameraNear = n;
    o.cameraFar = f;
    o.cameraFOV = fov;
    o.cameraAspectRatio = aspect;
    o.cameraPos[0] = o.cameraPos[1] = o.cameraPos[2] = 0.0f;
    o.cameraUp[0] = 0; o.cameraUp[1] = 1; o.cameraUp[2] = 0;
    o.cameraRight[0] = 1; o.cameraRight[1] = 0; o.cameraRight[2] = 0;
    o.cameraFwd[0] = 0; o.cameraFwd[1] = 0; o.cameraFwd[2] = 1;
    o.jitterOffset[0] = o.jitterOffset[1] = 0.0f;
    o.cameraPinholeOffset[0] = o.cameraPinholeOffset[1] = 0.0f;
    o.mvecScale[0] = 1.0f / float(w);  // MVs in pixels -> normalised
    o.mvecScale[1] = 1.0f / float(h);
    o.colorBuffersHDR = false;
    o.depthInverted = false;
    o.cameraMotionIncluded = true;
    o.automodeOverrideReset = false;
    o.notRenderingGameFrames = false;
    o.orthoProjection = false;
    o.motionVectorsInvalidValue = 3.402823466e38f;  // zero is valid (no motion)
    o.motionVectorsDilated = false;
    o.menuDetectionEnabled = false;
    o.mvecsSubrectSize = {w, h};
    o.depthSubrectSize = {w, h};
    o.backbufferSubrectSize = {w, h};
    o.outputInterpSubrectSize = {w, h};
}

}  // namespace spike
