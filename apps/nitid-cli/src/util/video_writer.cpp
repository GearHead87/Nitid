#include "util/video_writer.hpp"

#include <nitid/error.hpp>

#include <format>

namespace nitid_cli {

namespace {

std::string quote(const std::string& s)
{
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

} // namespace

VideoWriter::VideoWriter(const std::string& path, uint32_t w, uint32_t h, double fps, bool lossless)
    : frame_bytes_(size_t(w) * h * 4)
{
    // RGBA -> BT.709 limited-range YUV, tagged, so decoders convert it back the same way Nitid decoded it.
    std::string codec = lossless ? "-c:v ffv1 -level 3 -pix_fmt yuv444p" : "-c:v libx264 -preset fast -crf 12 -pix_fmt yuv420p";
    std::string cmd = std::format(
        "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s {}x{} -r {:.6f} -i - "
        "-vf scale=out_color_matrix=bt709:out_range=tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 "
        "-color_range tv {} {}",
        w, h, fps, codec, quote(path));
    pipe_ = popen(cmd.c_str(), "w");
    if (!pipe_) throw nitid::Error("cannot start ffmpeg to write " + path);
}

VideoWriter::~VideoWriter() { close(); }

bool VideoWriter::write(std::span<const uint8_t> rgba)
{
    return pipe_ && rgba.size() >= frame_bytes_ && std::fwrite(rgba.data(), 1, frame_bytes_, pipe_) == frame_bytes_;
}

bool VideoWriter::close()
{
    if (!pipe_) return true;
    int rc = pclose(pipe_);
    pipe_ = nullptr;
    return rc == 0;
}

} // namespace nitid_cli
