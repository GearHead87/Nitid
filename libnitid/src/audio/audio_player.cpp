#include "audio/audio_player.hpp"

#include "audio/av_sync.hpp"

#include "core/log.hpp"
#include "platform/pipewire_lib.hpp"

#include <spa/param/audio/format-utils.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace nitid::audio {

namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr size_t kRingFrames = kRate * 2;  // 2 s of decoded audio ahead

int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

struct AudioPlayer::Impl {
    // --- FFmpeg (decoder thread only, except open/seek with the thread stopped) ---
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    double start_s = 0;
    std::vector<float> conv;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::string description;

    // --- shared state, guarded by m ---
    mutable std::mutex m;
    std::condition_variable space;           // the decoder waits for room in the ring
    std::vector<float> ring = std::vector<float>(kRingFrames * kChannels);
    size_t rd = 0, count = 0;                // read position and frames available (in frames)
    double head_media = 0;                   // media time of ring[rd]
    bool eof = false;
    bool first_push = true;                  // next push is the first after a seek
    double drop_until = 0;                   // decoder: discard samples before this time (seek target)

    bool paused = false;
    bool resync = true;
    bool have_sync = false;
    double sync_media = 0;
    int64_t sync_when = 0;
    bool have_anchor = false;                // audio media time `anchor_media` is heard at `anchor_wall`
    double anchor_media = 0;
    int64_t anchor_wall = 0;
    double anchor_ratio = 1;
    double ratio = 1, frac = 0;
    size_t silence_pending = 0;
    float volume = 1;
    bool muted = false;
    double user_delay = 0;
    double err_ema = 0;
    bool err_init = false;
    int samples_since_resync = 0;    // sync() calls since the last resync
    int64_t resync_wall = 0;         // when the last resync happened
    double latency_s = 0;
    AudioStats st;

    // --- PipeWire ---
    const platform::PipeWireLib* pw = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_stream* pws = nullptr;

    ~Impl()
    {
        if (pw && loop) pw->thread_loop_stop(loop);
        if (pw && pws) pw->stream_destroy(pws);
        if (pw && loop) pw->thread_loop_destroy(loop);
        stop_thread();
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    }

    void stop_thread()
    {
        stop = true;
        space.notify_all();
        if (thread.joinable()) thread.join();
        stop = false;
    }

    void advance(size_t k)  // m held
    {
        rd = (rd + k) % kRingFrames;
        count -= k;
        head_media += double(k) / kRate;
    }

    // Decoder thread: appends interleaved stereo frames; blocks while the ring is full.
    bool push(const float* src, size_t n, double t_first)
    {
        std::unique_lock l(m);
        if (first_push) {
            first_push = false;
            if (t_first + double(n) / kRate <= drop_until) {
                first_push = true;  // whole chunk before the seek target
                return true;
            }
            if (t_first < drop_until) {
                size_t skip = std::min(n, size_t((drop_until - t_first) * kRate));
                src += skip * kChannels, n -= skip, t_first += double(skip) / kRate;
            }
            // Audio that starts after the target (e.g. a file whose sound begins later): fill with silence.
            size_t gap = t_first > head_media ? size_t((t_first - head_media) * kRate) : 0;
            gap = std::min(gap, kRingFrames / 2);
            for (size_t i = 0; i < gap; ++i) {
                size_t w = (rd + count) % kRingFrames;
                ring[w * 2] = ring[w * 2 + 1] = 0;
                ++count;
            }
        }
        while (n > 0) {
            space.wait(l, [&] { return stop || count < kRingFrames; });
            if (stop) return false;
            size_t k = std::min(n, kRingFrames - count);
            for (size_t i = 0; i < k; ++i) {
                size_t w = (rd + count + i) % kRingFrames;
                ring[w * 2] = src[i * 2];
                ring[w * 2 + 1] = src[i * 2 + 1];
            }
            count += k, src += k * kChannels, n -= k;
        }
        return true;
    }

    bool convert(AVFrame* f, double t)
    {
        int out = swr_get_out_samples(swr, f->nb_samples);
        if (out <= 0) return true;
        conv.resize(size_t(out) * kChannels);
        uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(conv.data())};
        int n = swr_convert(swr, dst, out, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
        if (n <= 0) return true;
        return push(conv.data(), size_t(n), t);
    }

    void decode_loop()
    {
        AVStream* s = fmt->streams[stream];
        double next_t = drop_until;
        auto drain = [&]() -> bool {
            for (;;) {
                int e = avcodec_receive_frame(dec, frame);
                if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) return true;
                if (e < 0) return true;
                double t = frame->best_effort_timestamp != AV_NOPTS_VALUE
                               ? double(frame->best_effort_timestamp) * av_q2d(s->time_base) - start_s
                               : next_t;
                next_t = t + double(frame->nb_samples) / std::max(1, frame->sample_rate);
                bool ok = convert(frame, t);
                av_frame_unref(frame);
                if (!ok) return false;
            }
        };
        while (!stop) {
            int e = av_read_frame(fmt, pkt);
            if (e < 0) {
                avcodec_send_packet(dec, nullptr);
                drain();
                std::lock_guard l(m);
                eof = true;
                break;
            }
            if (pkt->stream_index != stream) {
                av_packet_unref(pkt);
                continue;
            }
            avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (!drain()) break;
        }
    }

    void start_thread() { thread = std::thread([this] { decode_loop(); }); }

    // PipeWire's thread: fill one buffer.
    void process()
    {
        pw_buffer* b = pw->stream_dequeue_buffer(pws);
        if (!b) return;
        spa_data& d = b->buffer->datas[0];
        auto* dst = static_cast<float*>(d.data);
        if (dst) {
            const uint32_t stride = sizeof(float) * kChannels;
            uint32_t n = d.maxsize / stride;
            if (b->requested) n = std::min<uint32_t>(n, uint32_t(b->requested));
            pw_time t{};
            pw->stream_get_time_n(pws, &t, sizeof t);
            fill(dst, n, t);
            d.chunk->offset = 0;
            d.chunk->stride = int32_t(stride);
            d.chunk->size = n * stride;
        }
        pw->stream_queue_buffer(pws, b);
    }

    void fill(float* dst, uint32_t n, const pw_time& t)
    {
        std::unique_lock l(m);
        // When the first sample of this buffer will be heard.
        double delay = t.rate.denom ? double(t.delay) * t.rate.num / t.rate.denom : 0;
        delay += double(t.buffered) / kRate + double(t.queued) / (sizeof(float) * kChannels) / kRate;
        latency_s = delay;
        const int64_t play_wall = (t.now ? t.now : now_ns()) + int64_t(delay * 1e9);
        const float gain = muted ? 0.0f : volume;
        auto silence = [&](uint32_t from) { std::fill(dst + size_t(from) * kChannels, dst + size_t(n) * kChannels, 0.0f); };

        if (paused || !have_sync) {
            silence(0);
            have_anchor = false;
            return;
        }
        if (resync) {
            // Line the audio up with the video: the sample for `target` should be heard at play_wall.
            double target = sync_media + double(play_wall - sync_when) * 1e-9 - user_delay;
            double diff = target - head_media;
            if (diff > 0) {
                advance(std::min(count, size_t(diff * kRate)));
                if (head_media + 0.002 < target) {  // not decoded that far yet: wait
                    silence(0);
                    have_anchor = false;
                    space.notify_one();
                    return;
                }
            } else if (diff < 0) {
                silence_pending = size_t(-diff * kRate);
            }
            resync = false;
            frac = 0;
            err_init = false;
            samples_since_resync = 0;
            resync_wall = play_wall;
            ++st.resyncs;
        }
        uint32_t i = uint32_t(std::min<size_t>(n, silence_pending));
        silence_pending -= i;
        if (i) silence(0);
        if (i < n) {
            anchor_media = head_media + frac / kRate;
            anchor_wall = play_wall + int64_t(double(i) * 1e9 / kRate);
            anchor_ratio = ratio;
            have_anchor = true;
            double pos = frac;
            for (; i < n; ++i) {
                const size_t k = size_t(pos);
                if (k + 1 >= count) {  // decoder behind (or end of the audio)
                    if (!eof) ++st.underruns;
                    silence(i);
                    break;
                }
                const float w = float(pos - double(k));
                const float* a = &ring[((rd + k) % kRingFrames) * 2];
                const float* c = &ring[((rd + k + 1) % kRingFrames) * 2];
                dst[i * 2] = (a[0] + (c[0] - a[0]) * w) * gain;
                dst[i * 2 + 1] = (a[1] + (c[1] - a[1]) * w) * gain;
                pos += ratio;
            }
            size_t used = std::min(count, size_t(pos));
            frac = pos - double(used);
            advance(used);
        }
        space.notify_one();
    }

    static void on_process(void* data) { static_cast<Impl*>(data)->process(); }
};

AudioPlayer::AudioPlayer() : impl_(std::make_unique<Impl>()) {}
AudioPlayer::~AudioPlayer() = default;

std::unique_ptr<AudioPlayer> AudioPlayer::open(const std::string& path, std::string* note)
{
    std::string err;
    const auto* pw = platform::pipewire(&err);
    if (!pw) {
        *note = err;
        return nullptr;
    }
    std::unique_ptr<AudioPlayer> p(new AudioPlayer());
    Impl& m = *p->impl_;
    m.pw = pw;
    if (avformat_open_input(&m.fmt, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(m.fmt, nullptr) < 0) {
        *note = "can't read the file's audio";
        return nullptr;
    }
    const AVCodec* codec = nullptr;
    m.stream = av_find_best_stream(m.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m.stream < 0 || !codec) {
        *note = "no audio in this file";
        return nullptr;
    }
    AVStream* s = m.fmt->streams[m.stream];
    // Media time 0 = the video's first frame time base: use the container start like the video source does.
    AVStream* vs = nullptr;
    int vi = av_find_best_stream(m.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi >= 0) vs = m.fmt->streams[vi];
    if (vs && vs->start_time != AV_NOPTS_VALUE) m.start_s = double(vs->start_time) * av_q2d(vs->time_base);
    for (unsigned i = 0; i < m.fmt->nb_streams; ++i)
        if (int(i) != m.stream && int(i) != vi) m.fmt->streams[i]->discard = AVDISCARD_ALL;

    m.dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m.dec, s->codecpar);
    m.dec->pkt_timebase = s->time_base;
    if (avcodec_open2(m.dec, codec, nullptr) < 0) {
        *note = std::string("can't open the ") + codec->name + " audio decoder";
        return nullptr;
    }
    AVChannelLayout in = m.dec->ch_layout, out = AV_CHANNEL_LAYOUT_STEREO;
    if (in.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&in, in.nb_channels);
    if (swr_alloc_set_opts2(&m.swr, &out, AV_SAMPLE_FMT_FLT, kRate, &in, m.dec->sample_fmt, m.dec->sample_rate, 0, nullptr) < 0 ||
        swr_init(m.swr) < 0) {
        *note = "can't convert this audio format";
        return nullptr;
    }
    m.pkt = av_packet_alloc();
    m.frame = av_frame_alloc();
    m.description = std::format("{} {} Hz {} ch -> PipeWire {}", codec->name, m.dec->sample_rate, in.nb_channels, pw->version);
    m.st.description = m.description;

    // PipeWire playback stream: 48 kHz stereo float, ~21 ms buffers.
    m.loop = pw->thread_loop_new("nitid-audio", nullptr);
    static const pw_stream_events events = {.version = PW_VERSION_STREAM_EVENTS, .process = &Impl::on_process};
    pw->thread_loop_lock(m.loop);
    pw_properties* props = pw->properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                              PW_KEY_MEDIA_ROLE, "Movie", PW_KEY_APP_NAME, "Nitid", PW_KEY_NODE_NAME,
                                              "nitid", PW_KEY_NODE_LATENCY, "1024/48000", nullptr);
    m.pws = pw->stream_new_simple(pw->thread_loop_get_loop(m.loop), "Nitid", props, &events, &m);
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
    int r = m.pws ? pw->stream_connect(m.pws, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                       pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1)
                  : -1;
    pw->thread_loop_unlock(m.loop);
    if (r < 0) {
        *note = "PipeWire playback stream failed";
        return nullptr;
    }
    pw->thread_loop_start(m.loop);
    m.start_thread();
    log::info("Audio: {}", m.description);
    return p;
}

void AudioPlayer::seek(double seconds)
{
    Impl& m = *impl_;
    m.stop_thread();
    int64_t ts = int64_t((seconds + m.start_s) * AV_TIME_BASE);
    avformat_seek_file(m.fmt, -1, INT64_MIN, ts, ts, 0);
    avcodec_flush_buffers(m.dec);
    swr_close(m.swr);
    swr_init(m.swr);
    {
        std::lock_guard l(m.m);
        m.rd = m.count = 0;
        m.head_media = seconds;
        m.drop_until = seconds;
        m.first_push = true;
        m.eof = false;
        m.frac = 0;
        m.silence_pending = 0;
        m.have_anchor = false;
        m.have_sync = false;  // silent until the video shows the new position
        m.resync = true;
    }
    m.start_thread();
}

void AudioPlayer::set_paused(bool p)
{
    std::lock_guard l(impl_->m);
    if (impl_->paused == p) return;
    impl_->paused = p;
    impl_->have_anchor = false;
    if (!p) impl_->resync = true;
}

void AudioPlayer::set_volume(float g)
{
    std::lock_guard l(impl_->m);
    impl_->volume = std::max(0.0f, g);
}

void AudioPlayer::set_muted(bool mu)
{
    std::lock_guard l(impl_->m);
    impl_->muted = mu;
}

void AudioPlayer::set_delay(double s)
{
    std::lock_guard l(impl_->m);
    if (impl_->user_delay == s) return;
    impl_->user_delay = s;
    impl_->resync = true;
}

void AudioPlayer::sync(double media_s, int64_t when_ns)
{
    Impl& m = *impl_;
    std::lock_guard l(m.m);
    m.sync_media = media_s;
    m.sync_when = when_ns;
    m.have_sync = true;
    if (!m.have_anchor || m.paused || m.resync) return;
    const double heard = m.anchor_media + double(when_ns - m.anchor_wall) * 1e-9 * m.anchor_ratio;
    const double err = heard - (media_s - m.user_delay);  // + = audio early
    if (!m.err_init) {
        m.err_ema = err;
        m.err_init = true;
    } else {
        m.err_ema += (err - m.err_ema) * 0.05;
    }
    const double r = sync_ratio(m.err_ema);
    if (r == 0 || std::fabs(err) > 0.1) {  // the smoothed value lags: big jumps resync at once
        m.resync = true;
        return;
    }
    // Settling: right after a resync the picture can still move by a few frames (first frames after loading
    // are slow). If the gap is still clearly there once the error has settled, line up again instead of
    // resampling it away over seconds.
    if (++m.samples_since_resync == 30 && std::fabs(m.err_ema) > kSettleError && when_ns - m.resync_wall < 2'000'000'000) {
        m.resync = true;
        return;
    }
    m.ratio = r;
    m.st.sync_error_ms = m.err_ema * 1000;
    m.st.max_sync_error_ms = std::max(m.st.max_sync_error_ms, std::fabs(m.err_ema) * 1000);
}

AudioStats AudioPlayer::stats() const
{
    std::lock_guard l(impl_->m);
    AudioStats s = impl_->st;
    s.speed = impl_->ratio;
    s.latency_ms = impl_->latency_s * 1000;
    return s;
}

void AudioPlayer::reset_stats()
{
    std::lock_guard l(impl_->m);
    impl_->st.max_sync_error_ms = 0;
    impl_->st.resyncs = impl_->st.underruns = 0;
}

} // namespace nitid::audio
