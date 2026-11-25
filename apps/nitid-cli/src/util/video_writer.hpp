#pragma once

// Writes RGBA frames to a video file by piping them to the ffmpeg command-line tool. Good enough for
// tests and quality checks; the NVENC recorder (Phase 5) replaces it for real exports.
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace nitid_cli {

class VideoWriter {
public:
    // lossless: FFV1 (big files, exact); otherwise H.264 CRF 12 (visually lossless, small).
    VideoWriter(const std::string& path, uint32_t width, uint32_t height, double fps, bool lossless);
    ~VideoWriter();
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    bool write(std::span<const uint8_t> rgba);
    bool close();  // waits for ffmpeg; false if it failed

private:
    FILE* pipe_ = nullptr;
    size_t frame_bytes_ = 0;
};

} // namespace nitid_cli
