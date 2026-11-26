// Spatial upscalers on RGBA8 frames: AMD FidelityFX FSR 1 (EASU + RCAS) and NVIDIA Image Scaling (NIS).
// Compiled to PTX at build time and loaded through the driver API by gpu/spatial_scaler.cpp.
//
// Both are ports of the vendors' shader code to CUDA, kept as close to the originals as possible:
//  - FSR 1: ffx_fsr1.h (FsrEasuF, FsrRcasF; 32-bit float path), Copyright (c) 2021 Advanced Micro Devices, Inc.,
//    MIT license. github.com/GPUOpen-Effects/FidelityFX-FSR
//  - NIS: NIS_Scaler.h v1.0.3 (NVScaler), Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, MIT license.
//    github.com/NVIDIAGameWorks/NVIDIAImageScaling. The configuration and filter tables come from NIS_Config.h
//    (libnitid/third_party/nis/), computed on the CPU.
// Full license texts: packaging/linux/THIRD-PARTY-NOTICES.md.
//
// Frames are tightly packed RGBA8 (pitch = width * 4), display-referred (gamma) colour, as both algorithms expect.

// --- shared helpers --------------------------------------------------------------------------------------------

__device__ __forceinline__ float3 load_rgb(const uchar4* img, int w, int h, int x, int y)
{
    x = min(max(x, 0), w - 1);
    y = min(max(y, 0), h - 1);
    const uchar4 p = img[size_t(y) * w + x];
    return make_float3(p.x * (1.0f / 255.0f), p.y * (1.0f / 255.0f), p.z * (1.0f / 255.0f));
}

__device__ __forceinline__ unsigned char to8(float v)
{
    return (unsigned char)(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f + 0.5f);
}

__device__ __forceinline__ void store_rgb(uchar4* img, int w, int x, int y, float r, float g, float b)
{
    img[size_t(y) * w + x] = make_uchar4(to8(r), to8(g), to8(b), 255);
}

__device__ __forceinline__ float sat(float v) { return fminf(fmaxf(v, 0.0f), 1.0f); }

// ffx_a.h approximations (same bit tricks, so results match the shader).
__device__ __forceinline__ float prx_lo_rcp(float a) { return __uint_as_float(0x7ef07ebbu - __float_as_uint(a)); }
__device__ __forceinline__ float prx_lo_rsq(float a) { return __uint_as_float(0x5f347d74u - (__float_as_uint(a) >> 1)); }
__device__ __forceinline__ float prx_med_rcp(float a)
{
    float b = __uint_as_float(0x7ef19fffu - __float_as_uint(a));
    return b * (-b * a + 2.0f);
}

// --- FSR 1 EASU (edge-adaptive spatial upsampling) --------------------------------------------------------------

__device__ __forceinline__ void easu_tap(float3& aC, float& aW, float offx, float offy, float dirx, float diry, float lenx,
                                         float leny, float lob, float clp, float3 c)
{
    // Rotate offset by direction, then anisotropy.
    float vx = (offx * dirx) + (offy * diry);
    float vy = (offx * -diry) + (offy * dirx);
    vx *= lenx;
    vy *= leny;
    float d2 = fminf(vx * vx + vy * vy, clp);
    // Lanczos-2 approximation without sin/rcp/sqrt.
    float wB = (2.0f / 5.0f) * d2 - 1.0f;
    float wA = lob * d2 - 1.0f;
    wB *= wB;
    wA *= wA;
    wB = (25.0f / 16.0f) * wB - (25.0f / 16.0f - 1.0f);
    const float w = wB * wA;
    aC.x += c.x * w, aC.y += c.y * w, aC.z += c.z * w;
    aW += w;
}

// Direction and length from a '+' of lumas around c, weighted by the bilinear weight w.
__device__ __forceinline__ void easu_set(float& dirx, float& diry, float& len, float w, float lA, float lB, float lC, float lD,
                                         float lE)
{
    float dc = lD - lC, cb = lC - lB;
    float lenX = prx_lo_rcp(fmaxf(fabsf(dc), fabsf(cb)));
    float dirX = lD - lB;
    dirx += dirX * w;
    lenX = sat(fabsf(dirX) * lenX);
    len += lenX * lenX * w;
    float ec = lE - lC, ca = lC - lA;
    float lenY = prx_lo_rcp(fmaxf(fabsf(ec), fabsf(ca)));
    float dirY = lE - lA;
    diry += dirY * w;
    lenY = sat(fabsf(dirY) * lenY);
    len += lenY * lenY * w;
}

__device__ __forceinline__ float luma2(float3 c) { return c.z * 0.5f + (c.x * 0.5f + c.y); }  // luma times 2

// scale = src / dst; offset = 0.5 * scale - 0.5 (FsrEasuCon con0).
extern "C" __global__ void fsr_easu(const uchar4* src, int sw, int sh, uchar4* dst, int dw, int dh, float scale_x,
                                    float scale_y, float off_x, float off_y)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dw || y >= dh) return;

    float ppx = x * scale_x + off_x, ppy = y * scale_y + off_y;
    const float fx = floorf(ppx), fy = floorf(ppy);
    ppx -= fx, ppy -= fy;
    const int ix = int(fx), iy = int(fy);
    // 12-tap kernel:
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    const float3 b = load_rgb(src, sw, sh, ix, iy - 1), c = load_rgb(src, sw, sh, ix + 1, iy - 1);
    const float3 e = load_rgb(src, sw, sh, ix - 1, iy), f = load_rgb(src, sw, sh, ix, iy);
    const float3 g = load_rgb(src, sw, sh, ix + 1, iy), h = load_rgb(src, sw, sh, ix + 2, iy);
    const float3 i = load_rgb(src, sw, sh, ix - 1, iy + 1), j = load_rgb(src, sw, sh, ix, iy + 1);
    const float3 k = load_rgb(src, sw, sh, ix + 1, iy + 1), l = load_rgb(src, sw, sh, ix + 2, iy + 1);
    const float3 n = load_rgb(src, sw, sh, ix, iy + 2), o = load_rgb(src, sw, sh, ix + 1, iy + 2);
    const float bL = luma2(b), cL = luma2(c), eL = luma2(e), fL = luma2(f), gL = luma2(g), hL = luma2(h);
    const float iL = luma2(i), jL = luma2(j), kL = luma2(k), lL = luma2(l), nL = luma2(n), oL = luma2(o);

    float dirx = 0, diry = 0, len = 0;
    easu_set(dirx, diry, len, (1.0f - ppx) * (1.0f - ppy), bL, eL, fL, gL, jL);
    easu_set(dirx, diry, len, ppx * (1.0f - ppy), cL, fL, gL, hL, kL);
    easu_set(dirx, diry, len, (1.0f - ppx) * ppy, fL, iL, jL, kL, nL);
    easu_set(dirx, diry, len, ppx * ppy, gL, jL, kL, lL, oL);

    // Normalise the direction (approximation), clean up close to zero.
    float dirR = dirx * dirx + diry * diry;
    const bool zro = dirR < (1.0f / 32768.0f);
    dirR = prx_lo_rsq(dirR);
    dirR = zro ? 1.0f : dirR;
    dirx = zro ? 1.0f : dirx;
    dirx *= dirR, diry *= dirR;
    len = len * 0.5f;
    len *= len;
    // Stretch the kernel from 1.0 (horizontal/vertical) to sqrt(2) (diagonal).
    const float stretch = (dirx * dirx + diry * diry) * prx_lo_rcp(fmaxf(fabsf(dirx), fabsf(diry)));
    const float len2x = 1.0f + (stretch - 1.0f) * len, len2y = 1.0f - 0.5f * len;
    const float lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
    const float clp = prx_lo_rcp(lob);

    // Deringing limits: min/max of the 4 nearest (f g j k).
    const float3 mn = make_float3(fminf(fminf(f.x, g.x), fminf(j.x, k.x)), fminf(fminf(f.y, g.y), fminf(j.y, k.y)),
                                  fminf(fminf(f.z, g.z), fminf(j.z, k.z)));
    const float3 mx = make_float3(fmaxf(fmaxf(f.x, g.x), fmaxf(j.x, k.x)), fmaxf(fmaxf(f.y, g.y), fmaxf(j.y, k.y)),
                                  fmaxf(fmaxf(f.z, g.z), fmaxf(j.z, k.z)));

    float3 aC = make_float3(0, 0, 0);
    float aW = 0;
    easu_tap(aC, aW, 0.0f - ppx, -1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, b);
    easu_tap(aC, aW, 1.0f - ppx, -1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, c);
    easu_tap(aC, aW, -1.0f - ppx, 1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, i);
    easu_tap(aC, aW, 0.0f - ppx, 1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, j);
    easu_tap(aC, aW, 0.0f - ppx, 0.0f - ppy, dirx, diry, len2x, len2y, lob, clp, f);
    easu_tap(aC, aW, -1.0f - ppx, 0.0f - ppy, dirx, diry, len2x, len2y, lob, clp, e);
    easu_tap(aC, aW, 1.0f - ppx, 1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, k);
    easu_tap(aC, aW, 2.0f - ppx, 1.0f - ppy, dirx, diry, len2x, len2y, lob, clp, l);
    easu_tap(aC, aW, 2.0f - ppx, 0.0f - ppy, dirx, diry, len2x, len2y, lob, clp, h);
    easu_tap(aC, aW, 1.0f - ppx, 0.0f - ppy, dirx, diry, len2x, len2y, lob, clp, g);
    easu_tap(aC, aW, 1.0f - ppx, 2.0f - ppy, dirx, diry, len2x, len2y, lob, clp, o);
    easu_tap(aC, aW, 0.0f - ppx, 2.0f - ppy, dirx, diry, len2x, len2y, lob, clp, n);

    const float rw = 1.0f / aW;
    store_rgb(dst, dw, x, y, fminf(mx.x, fmaxf(mn.x, aC.x * rw)), fminf(mx.y, fmaxf(mn.y, aC.y * rw)),
              fminf(mx.z, fmaxf(mn.z, aC.z * rw)));
}

// --- FSR 1 RCAS (robust contrast-adaptive sharpening), same size ------------------------------------------------

#define FSR_RCAS_LIMIT (0.25f - (1.0f / 16.0f))

// sharp = exp2(-stops): 1.0 = strongest, each stop halves it (FsrRcasCon).
extern "C" __global__ void fsr_rcas(const uchar4* src, uchar4* dst, int w, int h, float sharp)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;
    //    b
    //  d e f
    //    h
    const float3 b = load_rgb(src, w, h, x, y - 1), d = load_rgb(src, w, h, x - 1, y), e = load_rgb(src, w, h, x, y);
    const float3 f = load_rgb(src, w, h, x + 1, y), hh = load_rgb(src, w, h, x, y + 1);
    const float mn4R = fminf(fminf(b.x, d.x), fminf(f.x, hh.x)), mx4R = fmaxf(fmaxf(b.x, d.x), fmaxf(f.x, hh.x));
    const float mn4G = fminf(fminf(b.y, d.y), fminf(f.y, hh.y)), mx4G = fmaxf(fmaxf(b.y, d.y), fmaxf(f.y, hh.y));
    const float mn4B = fminf(fminf(b.z, d.z), fminf(f.z, hh.z)), mx4B = fmaxf(fmaxf(b.z, d.z), fmaxf(f.z, hh.z));
    // Limiters (high-precision reciprocals).
    const float hitMinR = fminf(mn4R, e.x) / (4.0f * mx4R), hitMaxR = (1.0f - fmaxf(mx4R, e.x)) / (4.0f * mn4R - 4.0f);
    const float hitMinG = fminf(mn4G, e.y) / (4.0f * mx4G), hitMaxG = (1.0f - fmaxf(mx4G, e.y)) / (4.0f * mn4G - 4.0f);
    const float hitMinB = fminf(mn4B, e.z) / (4.0f * mx4B), hitMaxB = (1.0f - fmaxf(mx4B, e.z)) / (4.0f * mn4B - 4.0f);
    const float lobeR = fmaxf(-hitMinR, hitMaxR), lobeG = fmaxf(-hitMinG, hitMaxG), lobeB = fmaxf(-hitMinB, hitMaxB);
    const float lobe = fmaxf(-FSR_RCAS_LIMIT, fminf(fmaxf(fmaxf(lobeR, lobeG), lobeB), 0.0f)) * sharp;
    const float rcpL = prx_med_rcp(4.0f * lobe + 1.0f);
    store_rgb(dst, w, x, y, (lobe * (b.x + d.x + hh.x + f.x) + e.x) * rcpL, (lobe * (b.y + d.y + hh.y + f.y) + e.y) * rcpL,
              (lobe * (b.z + d.z + hh.z + f.z) + e.z) * rcpL);
}

// --- NVIDIA Image Scaling: NVScaler (1x to 2x) ------------------------------------------------------------------

#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
#define NIS_THREAD_GROUP_SIZE 256
#define kPhaseCount 64
#define kFilterSize 6
#define kSupportSize 6
#define kPadSize kSupportSize
#define kTilePitch (NIS_BLOCK_WIDTH + kPadSize)
#define kTileSize (kTilePitch * (NIS_BLOCK_HEIGHT + kPadSize))
#define kEdgeMapPitch (NIS_BLOCK_WIDTH + 2)
#define kEdgeMapSize (kEdgeMapPitch * (NIS_BLOCK_HEIGHT + 2))

// The NISConfig values the scaler uses (filled from NVScalerUpdateConfig on the CPU).
struct NisParams {
    float kDetectRatio, kDetectThres, kMinContrastRatio, kRatioNorm, kContrastBoost, kEps;
    float kSharpStartY, kSharpScaleY, kSharpStrengthMin, kSharpStrengthScale, kSharpLimitMin, kSharpLimitScale;
    float kScaleX, kScaleY;
    int src_w, src_h, dst_w, dst_h;
};

__device__ __forceinline__ float nis_y(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

__device__ float4 nis_edge_map(const float p[4][4], int i, int j, const NisParams& P)
{
    const float g_0 = fabsf(p[0 + i][0 + j] + p[0 + i][1 + j] + p[0 + i][2 + j] - p[2 + i][0 + j] - p[2 + i][1 + j] - p[2 + i][2 + j]);
    const float g_45 = fabsf(p[1 + i][0 + j] + p[0 + i][0 + j] + p[0 + i][1 + j] - p[2 + i][1 + j] - p[2 + i][2 + j] - p[1 + i][2 + j]);
    const float g_90 = fabsf(p[0 + i][0 + j] + p[1 + i][0 + j] + p[2 + i][0 + j] - p[0 + i][2 + j] - p[1 + i][2 + j] - p[2 + i][2 + j]);
    const float g_135 = fabsf(p[1 + i][0 + j] + p[2 + i][0 + j] + p[2 + i][1 + j] - p[0 + i][1 + j] - p[0 + i][2 + j] - p[1 + i][2 + j]);
    const float g_0_90_max = fmaxf(g_0, g_90), g_0_90_min = fminf(g_0, g_90);
    const float g_45_135_max = fmaxf(g_45, g_135), g_45_135_min = fminf(g_45, g_135);
    if (g_0_90_max + g_45_135_max == 0) return make_float4(0, 0, 0, 0);
    const float e_0_90 = fminf(g_0_90_max / (g_0_90_max + g_45_135_max), 1.0f);
    const float e_45_135 = 1.0f - e_0_90;
    const bool c_0_90 = (g_0_90_max > (g_0_90_min * P.kDetectRatio)) && (g_0_90_max > P.kDetectThres) && (g_0_90_max > g_45_135_min);
    const bool c_45_135 =
        (g_45_135_max > (g_45_135_min * P.kDetectRatio)) && (g_45_135_max > P.kDetectThres) && (g_45_135_max > g_0_90_min);
    const bool c_g_0_90 = g_0_90_max == g_0;
    const bool c_g_45_135 = g_45_135_max == g_45;
    const float f_e_0_90 = (c_0_90 && c_45_135) ? e_0_90 : 1.0f;
    const float f_e_45_135 = (c_0_90 && c_45_135) ? e_45_135 : 1.0f;
    return make_float4((c_0_90 && c_g_0_90) ? f_e_0_90 : 0.0f, (c_0_90 && !c_g_0_90) ? f_e_0_90 : 0.0f,
                       (c_45_135 && c_g_45_135) ? f_e_45_135 : 0.0f, (c_45_135 && !c_g_45_135) ? f_e_45_135 : 0.0f);
}

__device__ __forceinline__ float lerpf(float a, float b, float t) { return a + (b - a) * t; }
__device__ __forceinline__ float4 lerp4(float4 a, float4 b, float t)
{
    return make_float4(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t));
}

__device__ float nis_calc_lti(float p0, float p1, float p2, float p3, float p4, float p5, int phase_index, const NisParams& P)
{
    const bool selector = (phase_index <= kPhaseCount / 2);
    float sel = selector ? p0 : p3;
    const float a_min = fminf(fminf(p1, p2), sel), a_max = fmaxf(fmaxf(p1, p2), sel);
    sel = selector ? p2 : p5;
    const float b_min = fminf(fminf(p3, p4), sel), b_max = fmaxf(fmaxf(p3, p4), sel);
    const float a_cont = a_max - a_min, b_cont = b_max - b_min;
    const float cont_ratio = fmaxf(a_cont, b_cont) / (fminf(a_cont, b_cont) + P.kEps);
    return (1.0f - sat((cont_ratio - P.kMinContrastRatio) * P.kRatioNorm)) * P.kContrastBoost;
}

__device__ float nis_eval_poly6(const float pxl[6], int phase_int, const float (*coef)[kFilterSize],
                                const float (*usm)[kFilterSize], const NisParams& P)
{
    float y = 0.f, y_usm = 0.f;
#pragma unroll
    for (int i = 0; i < 6; ++i) y += coef[phase_int][i] * pxl[i];
#pragma unroll
    for (int i = 0; i < 6; ++i) y_usm += usm[phase_int][i] * pxl[i];
    // Piece-wise ramp based on luma: sharpen and limit as a function of luma.
    const float y_scale = 1.0f - sat((y - P.kSharpStartY) * P.kSharpScaleY);
    const float y_sharpness = y_scale * P.kSharpStrengthScale + P.kSharpStrengthMin;
    y_usm *= y_sharpness;
    const float y_sharpness_limit = (y_scale * P.kSharpLimitScale + P.kSharpLimitMin) * y;
    y_usm = fminf(y_sharpness_limit, fmaxf(-y_sharpness_limit, y_usm));
    y_usm *= nis_calc_lti(pxl[0], pxl[1], pxl[2], pxl[3], pxl[4], pxl[5], phase_int, P);  // reduce ringing
    return y + y_usm;
}

__device__ float nis_filter_normal(const float p[6][6], int phase_x, int phase_y, const float (*coef)[kFilterSize])
{
    float h_acc = 0.0f;
#pragma unroll
    for (int j = 0; j < 6; ++j) {
        float v_acc = 0.0f;
#pragma unroll
        for (int i = 0; i < 6; ++i) v_acc += p[i][j] * coef[phase_y][i];
        h_acc += v_acc * coef[phase_x][j];
    }
    return h_acc;
}

__device__ float nis_dir_filters(const float p[6][6], float phase_x_frac, float phase_y_frac, int phase_x_frac_int,
                                 int phase_y_frac_int, float4 w, const float (*coef)[kFilterSize],
                                 const float (*usm)[kFilterSize], const NisParams& P)
{
    float f = 0;
    if (w.x > 0.0f) {  // 0 deg
        float t[6];
#pragma unroll
        for (int i = 0; i < 6; ++i) t[i] = lerpf(p[i][2], p[i][3], phase_x_frac);
        f += nis_eval_poly6(t, phase_y_frac_int, coef, usm, P) * w.x;
    }
    if (w.y > 0.0f) {  // 90 deg
        float t[6];
#pragma unroll
        for (int i = 0; i < 6; ++i) t[i] = lerpf(p[2][i], p[3][i], phase_y_frac);
        f += nis_eval_poly6(t, phase_x_frac_int, coef, usm, P) * w.y;
    }
    if (w.z > 0.0f) {  // 45 deg
        float pb = 0.5f + 0.5f * (phase_x_frac - phase_y_frac);
        float t7[7];
        t7[1] = lerpf(p[2][1], p[1][2], pb);
        t7[3] = lerpf(p[3][2], p[2][3], pb);
        t7[5] = lerpf(p[4][3], p[3][4], pb);
        pb = pb - 0.5f;
        const float a = (pb >= 0.f) ? p[0][2] : p[2][0];
        const float b = (pb >= 0.f) ? p[1][3] : p[3][1];
        const float c = (pb >= 0.f) ? p[2][4] : p[4][2];
        const float d = (pb >= 0.f) ? p[3][5] : p[5][3];
        t7[0] = lerpf(p[1][1], a, fabsf(pb));
        t7[2] = lerpf(p[2][2], b, fabsf(pb));
        t7[4] = lerpf(p[3][3], c, fabsf(pb));
        t7[6] = lerpf(p[4][4], d, fabsf(pb));
        float t[6];
        float pp = phase_x_frac + phase_y_frac;
        const int o = pp >= 1 ? 1 : 0;
        if (pp >= 1) pp = pp - 1;
#pragma unroll
        for (int i = 0; i < 6; ++i) t[i] = t7[i + o];
        f += nis_eval_poly6(t, int(pp * 64), coef, usm, P) * w.z;
    }
    if (w.w > 0.0f) {  // 135 deg
        float pb = 0.5f * (phase_x_frac + phase_y_frac);
        float t7[7];
        t7[1] = lerpf(p[3][1], p[4][2], pb);
        t7[3] = lerpf(p[2][2], p[3][3], pb);
        t7[5] = lerpf(p[1][3], p[2][4], pb);
        pb = pb - 0.5f;
        const float a = (pb >= 0.f) ? p[5][2] : p[3][0];
        const float b = (pb >= 0.f) ? p[4][3] : p[2][1];
        const float c = (pb >= 0.f) ? p[3][4] : p[1][2];
        const float d = (pb >= 0.f) ? p[2][5] : p[0][3];
        t7[0] = lerpf(p[4][1], a, fabsf(pb));
        t7[2] = lerpf(p[3][2], b, fabsf(pb));
        t7[4] = lerpf(p[2][3], c, fabsf(pb));
        t7[6] = lerpf(p[1][4], d, fabsf(pb));
        float t[6];
        float pp = 1 + (phase_x_frac - phase_y_frac);
        const int o = pp >= 1 ? 1 : 0;
        if (pp >= 1) pp = pp - 1;
#pragma unroll
        for (int i = 0; i < 6; ++i) t[i] = t7[i + o];
        f += nis_eval_poly6(t, int(pp * 64), coef, usm, P) * w.w;
    }
    return f;
}

// Launch: grid (ceil(dst_w / 32), ceil(dst_h / 24)), block 256. coef_scaler / coef_usm: [64][6] floats.
extern "C" __global__ void __launch_bounds__(NIS_THREAD_GROUP_SIZE)
    nis_scaler(const uchar4* src, uchar4* dst, const float* coef_scaler, const float* coef_usm, NisParams P)
{
    __shared__ float shPixelsY[kTileSize];
    __shared__ float shCoefScaler[kPhaseCount][kFilterSize];
    __shared__ float shCoefUSM[kPhaseCount][kFilterSize];
    __shared__ float4 shEdgeMap[kEdgeMapSize];
    const unsigned tid = threadIdx.x;

    const int dstBlockX = NIS_BLOCK_WIDTH * blockIdx.x;
    const int dstBlockY = NIS_BLOCK_HEIGHT * blockIdx.y;
    const int srcBlockStartX = int(floorf((dstBlockX + 0.5f) * P.kScaleX - 0.5f));
    const int srcBlockStartY = int(floorf((dstBlockY + 0.5f) * P.kScaleY - 0.5f));
    const int srcBlockEndX = int(ceilf((dstBlockX + NIS_BLOCK_WIDTH + 0.5f) * P.kScaleX - 0.5f));
    const int srcBlockEndY = int(ceilf((dstBlockY + NIS_BLOCK_HEIGHT + 0.5f) * P.kScaleY - 0.5f));
    int numTilePixelsX = srcBlockEndX - srcBlockStartX + kSupportSize - 1;
    int numTilePixelsY = srcBlockEndY - srcBlockStartY + kSupportSize - 1;
    numTilePixelsX += numTilePixelsX & 0x1;  // even: loaded in 2x2 batches
    numTilePixelsY += numTilePixelsY & 0x1;
    const int numTilePixels = numTilePixelsX * numTilePixelsY;
    const int numEdgeMapPixelsX = numTilePixelsX - kSupportSize + 2;
    const int numEdgeMapPixelsY = numTilePixelsY - kSupportSize + 2;
    const int numEdgeMapPixels = numEdgeMapPixelsX * numEdgeMapPixelsY;

    // Luma tile: source pixels (start - 2 .. end + 3), clamped (the shader samples with a linear clamp sampler at
    // texel centres).
    for (unsigned i = tid * 2; i < unsigned(numTilePixels) >> 1; i += NIS_THREAD_GROUP_SIZE * 2) {
        const unsigned py = (i / numTilePixelsX) * 2;
        const unsigned px = i % numTilePixelsX;
        const int sx = srcBlockStartX + int(px) - 2, sy = srcBlockStartY + int(py) - 2;
        const unsigned idx = py * kTilePitch + px;
        shPixelsY[idx] = nis_y(load_rgb(src, P.src_w, P.src_h, sx, sy));
        shPixelsY[idx + 1] = nis_y(load_rgb(src, P.src_w, P.src_h, sx + 1, sy));
        shPixelsY[idx + kTilePitch] = nis_y(load_rgb(src, P.src_w, P.src_h, sx, sy + 1));
        shPixelsY[idx + kTilePitch + 1] = nis_y(load_rgb(src, P.src_w, P.src_h, sx + 1, sy + 1));
    }
    __syncthreads();
    // Edge map, 2x2 pixels at a time.
    for (unsigned i = tid * 2; i < unsigned(numEdgeMapPixels) >> 1; i += NIS_THREAD_GROUP_SIZE * 2) {
        const unsigned py = (i / numEdgeMapPixelsX) * 2;
        const unsigned px = i % numEdgeMapPixelsX;
        const unsigned edgeMapIdx = py * kEdgeMapPitch + px;
        const unsigned tileCornerIdx = (py + 1) * kTilePitch + px + 1;
        float p[4][4];
#pragma unroll
        for (int j = 0; j < 4; j++)
#pragma unroll
            for (int k = 0; k < 4; k++) p[j][k] = shPixelsY[tileCornerIdx + j * kTilePitch + k];
        shEdgeMap[edgeMapIdx] = nis_edge_map(p, 0, 0, P);
        shEdgeMap[edgeMapIdx + 1] = nis_edge_map(p, 0, 1, P);
        shEdgeMap[edgeMapIdx + kEdgeMapPitch] = nis_edge_map(p, 1, 0, P);
        shEdgeMap[edgeMapIdx + kEdgeMapPitch + 1] = nis_edge_map(p, 1, 1, P);
    }
    for (unsigned i = tid; i < kPhaseCount * kFilterSize; i += NIS_THREAD_GROUP_SIZE) {
        (&shCoefScaler[0][0])[i] = coef_scaler[i];
        (&shCoefUSM[0][0])[i] = coef_usm[i];
    }
    __syncthreads();

    const int posx = int(tid % NIS_BLOCK_WIDTH), posy = int(tid / NIS_BLOCK_WIDTH);
    const int dstX = dstBlockX + posx;
    const float srcX = (0.5f + dstX) * P.kScaleX - 0.5f;
    const int px = int(floorf(srcX) - srcBlockStartX);
    const float fx = srcX - floorf(srcX);
    const int fx_int = int(fx * kPhaseCount);
    if (dstX >= P.dst_w) return;  // after the last __syncthreads()

    for (int k = 0; k < NIS_BLOCK_WIDTH * NIS_BLOCK_HEIGHT / NIS_THREAD_GROUP_SIZE; ++k) {
        const int dstY = dstBlockY + posy + k * (NIS_THREAD_GROUP_SIZE / NIS_BLOCK_WIDTH);
        if (dstY >= P.dst_h) break;
        const float srcY = (0.5f + dstY) * P.kScaleY - 0.5f;
        const int py = int(floorf(srcY) - srcBlockStartY);
        const float fy = srcY - floorf(srcY);
        const int fy_int = int(fy * kPhaseCount);

        // Directional filter weights, interpolated from the 2x2 edge map around the position.
        const int e0 = py * kEdgeMapPitch + px;
        const float4 h0 = lerp4(shEdgeMap[e0], shEdgeMap[e0 + 1], fx);
        const float4 h1 = lerp4(shEdgeMap[e0 + kEdgeMapPitch], shEdgeMap[e0 + kEdgeMapPitch + 1], fx);
        const float4 w = lerp4(h0, h1, fy);

        const int t0 = py * kTilePitch + px;
        float p[6][6];
#pragma unroll
        for (int i = 0; i < 6; ++i)
#pragma unroll
            for (int j = 0; j < 6; ++j) p[i][j] = shPixelsY[t0 + i * kTilePitch + j];

        const float baseWeight = 1.0f - w.x - w.y - w.z - w.w;
        float opY = nis_filter_normal(p, fx_int, fy_int, shCoefScaler) * baseWeight;
        opY += nis_dir_filters(p, fx, fy, fx_int, fy_int, w, shCoefScaler, shCoefUSM, P);

        // Chroma: bilinear tap, then shift by the luma correction.
        const int bx = int(floorf(srcX)), by = int(floorf(srcY));
        const float3 c00 = load_rgb(src, P.src_w, P.src_h, bx, by), c10 = load_rgb(src, P.src_w, P.src_h, bx + 1, by);
        const float3 c01 = load_rgb(src, P.src_w, P.src_h, bx, by + 1), c11 = load_rgb(src, P.src_w, P.src_h, bx + 1, by + 1);
        float3 op = make_float3(lerpf(lerpf(c00.x, c10.x, fx), lerpf(c01.x, c11.x, fx), fy),
                                lerpf(lerpf(c00.y, c10.y, fx), lerpf(c01.y, c11.y, fx), fy),
                                lerpf(lerpf(c00.z, c10.z, fx), lerpf(c01.z, c11.z, fx), fy));
        const float corr = opY - nis_y(op);
        store_rgb(dst, P.dst_w, dstX, dstY, op.x + corr, op.y + corr, op.z + corr);
    }
}
