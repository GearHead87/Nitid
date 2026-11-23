// Tiny binary PPM (P6) reader/writer for the spikes; RGB8 <-> RGBA8 buffers.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace spike {

inline std::vector<uint8_t> read_ppm_rgba(const std::string& path, uint32_t& w, uint32_t& h)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(1);
    }
    int maxv = 0;
    if (std::fscanf(f, "P6 %u %u %d", &w, &h, &maxv) != 3 || maxv != 255) {
        std::fprintf(stderr, "bad ppm %s\n", path.c_str());
        std::exit(1);
    }
    std::fgetc(f);
    std::vector<uint8_t> rgb(size_t(w) * h * 3), rgba(size_t(w) * h * 4);
    if (std::fread(rgb.data(), 1, rgb.size(), f) != rgb.size()) {
        std::fprintf(stderr, "short ppm %s\n", path.c_str());
        std::exit(1);
    }
    std::fclose(f);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

inline void write_ppm_rgba(const std::string& path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(1);
    }
    std::fprintf(f, "P6 %u %u 255\n", w, h);
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

}  // namespace spike
