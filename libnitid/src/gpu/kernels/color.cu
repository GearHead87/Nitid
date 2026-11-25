// GPU colour conversion for decoded video. Compiled to PTX at build time (no CUDA runtime needed) and
// loaded through the driver API by gpu/kernels.cpp.
//
// Input: NVDEC output, semi-planar YUV 4:2:0: NV12 (8-bit) or P010/P016 (16-bit, MSB-aligned).
// Output: RGBA8, full range, same size.

struct YuvToRgb {
    float ry_cr, gy_cb, gy_cr, by_cb;  // R = Y + ry_cr*Cr, G = Y + gy_cb*Cb + gy_cr*Cr, B = Y + by_cb*Cb
    float y_off, y_scale, c_off, c_scale;  // normalised: Y' = (y - y_off) * y_scale, C = (c - c_off) * c_scale
};

__device__ __forceinline__ float load(const unsigned char* base, size_t pitch, int x, int y, int bytes, int comps, int c)
{
    const unsigned char* row = base + size_t(y) * pitch;
    if (bytes == 1) return row[x * comps + c] * (1.0f / 255.0f);
    return reinterpret_cast<const unsigned short*>(row)[x * comps + c] * (1.0f / 65535.0f);
}

__device__ __forceinline__ unsigned char to8(float v)
{
    v = fminf(fmaxf(v, 0.0f), 1.0f);
    return (unsigned char)(v * 255.0f + 0.5f);
}

extern "C" __global__ void yuv420sp_to_rgba(const unsigned char* y_plane, size_t y_pitch, const unsigned char* uv_plane,
                                            size_t uv_pitch, unsigned char* out, size_t out_pitch, int w, int h, int bytes,
                                            YuvToRgb m)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;

    const float Y = (load(y_plane, y_pitch, x, y, bytes, 1, 0) - m.y_off) * m.y_scale;

    // Chroma: 4:2:0, horizontally co-sited with even luma columns, vertically centred (MPEG-2/H.26x
    // default). Bilinear between the four nearest chroma samples.
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    float cx = x * 0.5f, cy = y * 0.5f - 0.25f;
    int x0 = int(floorf(cx)), y0 = int(floorf(cy));
    float fx = cx - x0, fy = cy - y0;
    int x1 = min(x0 + 1, cw - 1), y1 = min(y0 + 1, ch - 1);
    x0 = max(x0, 0), y0 = max(y0, 0);
    float cb = 0, cr = 0;
    {
        float u00 = load(uv_plane, uv_pitch, x0, y0, bytes, 2, 0), v00 = load(uv_plane, uv_pitch, x0, y0, bytes, 2, 1);
        float u10 = load(uv_plane, uv_pitch, x1, y0, bytes, 2, 0), v10 = load(uv_plane, uv_pitch, x1, y0, bytes, 2, 1);
        float u01 = load(uv_plane, uv_pitch, x0, y1, bytes, 2, 0), v01 = load(uv_plane, uv_pitch, x0, y1, bytes, 2, 1);
        float u11 = load(uv_plane, uv_pitch, x1, y1, bytes, 2, 0), v11 = load(uv_plane, uv_pitch, x1, y1, bytes, 2, 1);
        float u0 = u00 + (u10 - u00) * fx, u1 = u01 + (u11 - u01) * fx;
        float v0 = v00 + (v10 - v00) * fx, v1 = v01 + (v11 - v01) * fx;
        cb = ((u0 + (u1 - u0) * fy) - m.c_off) * m.c_scale;
        cr = ((v0 + (v1 - v0) * fy) - m.c_off) * m.c_scale;
    }
    unsigned char* o = out + size_t(y) * out_pitch + size_t(x) * 4;
    o[0] = to8(Y + m.ry_cr * cr);
    o[1] = to8(Y + m.gy_cb * cb + m.gy_cr * cr);
    o[2] = to8(Y + m.by_cb * cb);
    o[3] = 255;
}

// Luma thumbnail for scene-cut detection: average of each `block` x `block` area, full range 0..255.
extern "C" __global__ void luma_thumbnail(const unsigned char* y_plane, size_t y_pitch, int w, int h, int bytes, int block,
                                          float y_off, float y_scale, unsigned char* out, int tw, int th)
{
    const int tx = blockIdx.x * blockDim.x + threadIdx.x;
    const int ty = blockIdx.y * blockDim.y + threadIdx.y;
    if (tx >= tw || ty >= th) return;
    float sum = 0;
    int n = 0;
    for (int dy = 0; dy < block; ++dy)
        for (int dx = 0; dx < block; ++dx) {
            int x = tx * block + dx, y = ty * block + dy;
            if (x < w && y < h) {
                sum += load(y_plane, y_pitch, x, y, bytes, 1, 0);
                ++n;
            }
        }
    out[ty * tw + tx] = to8((sum / max(n, 1) - y_off) * y_scale);
}
