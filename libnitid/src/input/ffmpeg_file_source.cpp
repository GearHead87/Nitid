#include "input/ffmpeg_file_source.hpp"

#include "core/check.hpp"
#include "core/log.hpp"
#include "core/running_average.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <filesystem>

namespace nitid::input {

namespace {

std::string av_err(int e)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(e, buf, sizeof buf);
    return buf;
}

bool supports_cuda(const AVCodec* c)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(c, i);
        if (!cfg) return false;
        if (cfg->device_type == AV_HWDEVICE_TYPE_CUDA && (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) return true;
    }
}

// The default decoder for a codec may not support NVDEC (e.g. libdav1d for AV1). Prefer one that does.
const AVCodec* nvdec_capable_decoder(const AVCodec* preferred)
{
    if (supports_cuda(preferred)) return preferred;
    void* it = nullptr;
    while (const AVCodec* c = av_codec_iterate(&it))
        if (av_codec_is_decoder(c) && c->id == preferred->id && supports_cuda(c)) return c;
    return nullptr;
}

AVPixelFormat pick_cuda_format(AVCodecContext*, const AVPixelFormat* fmts)
{
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_CUDA) return *p;
    return fmts[0];  // decoder can't use NVDEC for this stream: software
}

int sws_colorspace(AVColorSpace cs)
{
    switch (cs) {
        case AVCOL_SPC_BT709: return SWS_CS_ITU709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG: return SWS_CS_ITU601;
        default: return -1;  // unspecified: guess from size below
    }
}

} // namespace

struct FfmpegFileSource::Ffmpeg {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVBufferRef* hw = nullptr;
    SwsContext* sws = nullptr;
    int stream = -1;
    AVPixelFormat sws_fmt = AV_PIX_FMT_NONE;
    int sws_w = 0, sws_h = 0;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw = nullptr;
    int64_t decoded = 0;  // frames out of the decoder since (re)start
    int64_t kept = 0;     // frames delivered (after source_step)

    ~Ffmpeg()
    {
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_frame_free(&sw);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        av_buffer_unref(&hw);
        avformat_close_input(&fmt);
    }
};

FfmpegFileSource::FfmpegFileSource(const std::string& path, const OpenOptions& options, int cuda_device)
    : ff_(std::make_unique<Ffmpeg>()), opts_(options)
{
    opts_.source_step = std::max(1, opts_.source_step);
    Ffmpeg& f = *ff_;
    int e = avformat_open_input(&f.fmt, path.c_str(), nullptr, nullptr);
    if (e < 0) failf("Can't open {}: {}", path, av_err(e));
    if ((e = avformat_find_stream_info(f.fmt, nullptr)) < 0) failf("Can't read {}: {}", path, av_err(e));
    const AVCodec* codec = nullptr;
    f.stream = av_find_best_stream(f.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (f.stream < 0 || !codec) failf("No playable video stream in {}", path);
    AVStream* st = f.fmt->streams[f.stream];
    const AVCodec* hw_codec = opts_.hw_decode ? nvdec_capable_decoder(codec) : nullptr;
    if (hw_codec) codec = hw_codec;

    f.dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(f.dec, st->codecpar);
    f.dec->pkt_timebase = st->time_base;
    std::string decoder = "software";
    if (hw_codec) {
        std::string dev = std::to_string(cuda_device);
        if (av_hwdevice_ctx_create(&f.hw, AV_HWDEVICE_TYPE_CUDA, dev.c_str(), nullptr, 0) == 0) {
            f.dec->hw_device_ctx = av_buffer_ref(f.hw);
            f.dec->get_format = pick_cuda_format;
            decoder = "NVDEC (cuda)";
        } else {
            log::warn("NVDEC not available, decoding in software");
        }
    } else if (opts_.hw_decode) {
        log::info("{} has no NVDEC decoder in this FFmpeg build; decoding in software", codec->name);
    }
    if (decoder == "software") decoder = std::string("software (") + codec->name + ")";
    f.dec->thread_count = 0;  // auto (software decoding; ignored by NVDEC)
    if ((e = avcodec_open2(f.dec, codec, nullptr)) < 0) failf("Can't open the {} decoder: {}", codec->name, av_err(e));
    f.pkt = av_packet_alloc();
    f.frame = av_frame_alloc();
    f.sw = av_frame_alloc();

    AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    double fps = fr.num && fr.den ? av_q2d(fr) : 30.0;
    info_.name = std::filesystem::path(path).filename().string();
    info_.codec = codec->name;
    info_.decoder = decoder;
    info_.width = uint32_t(st->codecpar->width);
    info_.height = uint32_t(st->codecpar->height);
    info_.fps = fps / opts_.source_step;
    info_.duration_s = f.fmt->duration > 0 ? double(f.fmt->duration) / AV_TIME_BASE : 0;
    log::info("Opened {}: {} {}x{} @ {:.3f} fps, {:.1f} s, decoder {}{}", info_.name, info_.codec, info_.width,
              info_.height, fps, info_.duration_s, decoder,
              opts_.source_step > 1 ? std::format(", using every {} frames", opts_.source_step) : "");
    start_thread();
}

FfmpegFileSource::~FfmpegFileSource() { stop_thread(); }

void FfmpegFileSource::start_thread()
{
    stop_ = false;
    queue_.reopen();
    thread_ = std::thread([this] { decode_loop(); });
}

void FfmpegFileSource::stop_thread()
{
    stop_ = true;
    queue_.close();
    if (thread_.joinable()) thread_.join();
}

void FfmpegFileSource::restart()
{
    stop_thread();
    Ffmpeg& f = *ff_;
    av_seek_frame(f.fmt, f.stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(f.dec);
    f.decoded = f.kept = 0;
    cuts_.reset();
    start_thread();
}

std::optional<DecodedFrame> FfmpegFileSource::next() { return queue_.pop(); }

void FfmpegFileSource::recycle(std::vector<uint8_t>&& buffer)
{
    std::lock_guard l(pool_mutex_);
    if (pool_.size() < 8) pool_.push_back(std::move(buffer));
}

void FfmpegFileSource::decode_loop()
{
    Ffmpeg& f = *ff_;
    bool eof = false;
    auto handle_frame = [&](AVFrame* fr) -> bool {
        auto t0 = SteadyClock::now();
        bool keep = (f.decoded++ % opts_.source_step) == 0;
        if (!keep) return true;
        AVFrame* src = fr;
        if (fr->format == AV_PIX_FMT_CUDA) {
            av_frame_unref(f.sw);
            int e = av_hwframe_transfer_data(f.sw, fr, 0);
            if (e < 0) {
                log::error("NVDEC frame download failed: {}", av_err(e));
                return false;
            }
            f.sw->colorspace = fr->colorspace;
            f.sw->color_range = fr->color_range;
            src = f.sw;
        }
        auto fmt = AVPixelFormat(src->format);
        if (!f.sws || fmt != f.sws_fmt || src->width != f.sws_w || src->height != f.sws_h) {
            sws_freeContext(f.sws);
            f.sws = sws_alloc_context();
            av_opt_set_int(f.sws, "srcw", src->width, 0);
            av_opt_set_int(f.sws, "srch", src->height, 0);
            av_opt_set_int(f.sws, "src_format", fmt, 0);
            av_opt_set_int(f.sws, "dstw", src->width, 0);
            av_opt_set_int(f.sws, "dsth", src->height, 0);
            av_opt_set_int(f.sws, "dst_format", AV_PIX_FMT_RGBA, 0);
            av_opt_set_int(f.sws, "sws_flags", SWS_BILINEAR | SWS_ACCURATE_RND, 0);
            av_opt_set_int(f.sws, "threads", 8, 0);
            if (sws_init_context(f.sws, nullptr, nullptr) < 0) {
                log::error("Can't convert {} to RGBA", av_get_pix_fmt_name(fmt));
                return false;
            }
            int cs = sws_colorspace(src->colorspace);
            if (cs < 0) cs = src->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
            int full_range = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
            sws_setColorspaceDetails(f.sws, sws_getCoefficients(cs), full_range, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0,
                                     1 << 16, 1 << 16);
            f.sws_fmt = fmt, f.sws_w = src->width, f.sws_h = src->height;
            log::debug("colour conversion: {} {}x{} -> RGBA", av_get_pix_fmt_name(fmt), src->width, src->height);
        }
        DecodedFrame out;
        out.width = uint32_t(src->width);
        out.height = uint32_t(src->height);
        {
            std::lock_guard l(pool_mutex_);
            if (!pool_.empty()) {
                out.rgba = std::move(pool_.back());
                pool_.pop_back();
            }
        }
        out.rgba.resize(size_t(out.width) * out.height * 4);
        uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        int dst_stride[4] = {int(out.width * 4), 0, 0, 0};
        sws_scale(f.sws, src->data, src->linesize, 0, src->height, dst, dst_stride);
        out.index = f.kept++;
        out.time_s = double(out.index) / info_.fps;
        out.scene_cut = detect_cuts_ && cuts_.push(out.rgba.data(), out.width, out.height);
        if (out.scene_cut) log::debug("scene cut at frame {} (score {:.2f})", out.index, cuts_.last_score());
        out.decode_ms = ms_since(t0);
        return queue_.push(std::move(out));
    };

    while (!stop_ && !eof) {
        int e = av_read_frame(f.fmt, f.pkt);
        if (e < 0) {
            eof = true;
            avcodec_send_packet(f.dec, nullptr);  // flush
        } else if (f.pkt->stream_index != f.stream) {
            av_packet_unref(f.pkt);
            continue;
        } else {
            avcodec_send_packet(f.dec, f.pkt);
            av_packet_unref(f.pkt);
        }
        while (!stop_) {
            e = avcodec_receive_frame(f.dec, f.frame);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) break;
            if (e < 0) {
                log::error("decode error: {}", av_err(e));
                break;
            }
            bool ok = handle_frame(f.frame);
            av_frame_unref(f.frame);
            if (!ok) {
                stop_ = true;
                break;
            }
        }
    }
    queue_.finish();
}

} // namespace nitid::input
