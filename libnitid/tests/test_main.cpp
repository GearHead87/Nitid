// libnitid unit tests: frame layout, frame selection, display clock, scene cuts, queue.
// Tiny self-contained harness (no test framework dependency).
#include "audio/av_sync.hpp"
#include "core/bounded_queue.hpp"
#include "input/scene_cut.hpp"
#include "pipeline/frame_clock.hpp"
#include "pipeline/frame_layout.hpp"

#include <nitid/settings.hpp>

#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int failures = 0, checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (!(cond)) {                                                             \
            ++failures;                                                            \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                          \
    } while (0)

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

using namespace nitid;
using namespace nitid::pipeline;

void test_layout()
{
    // 16:9 into a 16:10 box -> width-limited, letterboxed.
    Size f = fit({1920, 1080}, {2560, 1600});
    CHECK(f.w == 2560 && f.h == 1440);
    Rect r = centre(f, {2560, 1600});
    CHECK(r.x == 0 && r.y == 80 && r.w == 2560 && r.h == 1440);
    // 4:3 into 16:9 -> pillarbox.
    f = fit({640, 480}, {1920, 1080});
    CHECK(f.w == 1440 && f.h == 1080);
    // Upscale target: bigger box -> fit size; smaller box -> source.
    CHECK((upscale_target({1920, 1080}, {2560, 1440}) == Size{2560, 1440}));
    CHECK((upscale_target({1920, 1080}, {1280, 720}) == Size{1920, 1080}));
    CHECK((upscale_target({1920, 1080}, {1920, 1080}) == Size{1920, 1080}));
    // Capped at 4x.
    CHECK((upscale_target({320, 180}, {3840, 2160}) == Size{1280, 720}));
    // Degenerate input.
    CHECK((fit({0, 0}, {100, 100}) == Size{}));
    // Odd sizes become even.
    f = fit({1000, 1000}, {999, 999});
    CHECK(f.w % 2 == 0 && f.h % 2 == 0 && f.w <= 999);
}

void test_pick()
{
    FramePick p = pick(2.0, 0);
    CHECK(p.frame == 2 && p.phase == 0);
    p = pick(2.25, 0);
    CHECK(p.frame == 2 && near(p.phase, 0.25));
    // 2x rounds 0.75 down to 0.5; 4x keeps 0.75.
    p = pick(2.75, 2);
    CHECK(p.frame == 2 && near(p.phase, 0.5));
    p = pick(2.75, 4);
    CHECK(p.frame == 2 && near(p.phase, 0.75));
    // Floating-point noise just below an integer still counts as that frame.
    p = pick(3.0 - 1e-12, 2);
    CHECK(p.frame == 3 && p.phase == 0);
    // Headless 3x: n / 3 gives phases 0, 1/3, 2/3.
    for (int n = 0; n < 30; ++n) {
        p = pick(double(n) / 3, 3);
        CHECK(p.frame == n / 3);
        CHECK(near(p.phase, (n % 3) / 3.0, 1e-5));
    }
}

void test_clock()
{
    DisplayClock c;
    const double period = 1.0 / 165;
    double t0 = 100.0;
    CHECK(c.tick(t0, period, false) == 0);
    // Normal refreshes: exactly one period each, even with jitter in the wall clock.
    double t = t0;
    for (int i = 1; i <= 165; ++i) {
        t += period + ((i % 2) ? 0.0005 : -0.0005);
        c.tick(t, period, false);
    }
    CHECK(near(c.media_s(), 1.0, 1e-9));
    // A stall of 3 refreshes advances 3 periods.
    t += 3 * period;
    c.tick(t, period, false);
    CHECK(near(c.media_s(), 1.0 + 3 * period, 1e-9));
    // A long stall (window drag, loading) does not jump ahead: one refresh only.
    double before = c.media_s();
    t += 0.5;
    c.tick(t, period, false);
    CHECK(near(c.media_s(), before + period, 1e-9));
    // Paused: no movement.
    double m = c.media_s();
    t += period;
    c.tick(t, period, true);
    CHECK(c.media_s() == m);
    // Sustained slow frames (1.4 refreshes each): media time keeps up with the wall clock, in whole refreshes.
    DisplayClock slow;
    double ts = 0;
    slow.tick(ts, period, false);
    for (int i = 0; i < 100; ++i) {
        ts += 1.4 * period;
        double m2 = slow.tick(ts, period, false);
        double r = m2 / period;
        CHECK(near(r, std::round(r), 1e-6));  // always on the refresh grid
    }
    CHECK(std::fabs(slow.media_s() - ts) <= period);
    // 24 fps film at 165 Hz with frame gen: position advances 24/165 frames per refresh.
    DisplayClock d;
    d.tick(0, period, false);
    for (int i = 1; i <= 165; ++i) d.tick(i * period, period, false);
    CHECK(near(d.media_s() * 24, 24.0, 1e-6));
}

std::vector<uint8_t> solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint8_t> v(size_t(w) * h * 4);
    for (size_t i = 0; i < v.size(); i += 4) v[i] = r, v[i + 1] = g, v[i + 2] = b, v[i + 3] = 255;
    return v;
}

std::vector<uint8_t> gradient(uint32_t w, uint32_t h, int shift)
{
    std::vector<uint8_t> v(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t l = uint8_t(((x + shift) * 255 / w) % 256);
            size_t i = (size_t(y) * w + x) * 4;
            v[i] = v[i + 1] = v[i + 2] = l, v[i + 3] = 255;
        }
    return v;
}

void test_scene_cut()
{
    input::SceneCutDetector d;
    const uint32_t w = 320, h = 180;
    auto a = gradient(w, h, 0);
    CHECK(!d.push(a.data(), w, h));  // first frame is never a cut
    // A small pan keeps the histogram: not a cut.
    auto b = gradient(w, h, 8);
    CHECK(!d.push(b.data(), w, h));
    CHECK(d.last_score() < 0.1f);
    // Same frame again: score 0.
    CHECK(!d.push(b.data(), w, h));
    CHECK(d.last_score() == 0.f);
    // Switch to a dark solid frame: a cut.
    auto c = solid(w, h, 10, 10, 10);
    CHECK(d.push(c.data(), w, h));
    CHECK(d.last_score() > 0.9f);
    // A fade (brightness ramps a little every frame) is not a cut, even when each step is large-ish.
    input::SceneCutDetector f;
    for (int i = 0; i <= 12; ++i) {
        uint8_t v = uint8_t(i * 20);
        auto fr = solid(w, h, v, v, v);
        bool cut = f.push(fr.data(), w, h);
        if (i >= 2) CHECK(!cut);
    }
    // Two shots with the same brightness histogram but different layout: caught by the thumbnail measure.
    input::SceneCutDetector g;
    auto left_bright = gradient(w, h, 0);         // dark -> bright, left to right
    std::vector<uint8_t> mirrored(left_bright.size());
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) mirrored[(size_t(y) * w + x) * 4 + c] = left_bright[(size_t(y) * w + (w - 1 - x)) * 4 + c];
    CHECK(!g.push(left_bright.data(), w, h));
    CHECK(!g.push(left_bright.data(), w, h));
    CHECK(g.push(mirrored.data(), w, h));
    CHECK(g.last_score() < g.hist_threshold);  // the histogram measure alone would have missed it
    // Reset: next frame is a first frame again.
    d.reset();
    CHECK(!d.push(a.data(), w, h));
}

void test_queue()
{
    BoundedQueue<int> q(2);
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i) q.push(i);
        q.finish();
    });
    int expect = 0;
    while (auto v = q.pop()) CHECK(*v == expect++);
    producer.join();
    CHECK(expect == 100);
    // close() unblocks a waiting consumer.
    BoundedQueue<int> q2(1);
    std::thread consumer([&] { CHECK(!q2.pop().has_value()); });
    q2.close();
    consumer.join();
}

void test_settings()
{
    Settings s;
    CHECK(parse_upscaling("fsr", s) && s.upscaler == Upscaler::Fsr1 && upscaling_name(s) == "FSR 1");
    CHECK(parse_upscaling("nis", s) && s.upscaler == Upscaler::Nis && upscaling_name(s) == "NIS");
    CHECK(parse_upscaling("ultra", s) && s.upscaler == Upscaler::VfxSuperRes && upscaling_name(s) == "VSR ultra");
    CHECK(parse_upscaling("off", s) && upscaling_name(s) == "off");
    CHECK(!parse_upscaling("lanczos", s));
    CHECK(parse_upscaling("dlss", s) && s.upscaler == Upscaler::Dlss && s.dlss_preset == DlssPreset::Auto &&
          upscaling_name(s) == "DLSS");
    CHECK(parse_upscaling("dlss-k", s) && s.dlss_preset == DlssPreset::K && upscaling_name(s) == "DLSS preset K");
    CHECK(parse_upscaling("dlss-m", s) && s.dlss_preset == DlssPreset::M);
    CHECK(!parse_upscaling("dlss-x", s) && !parse_upscaling("dlssk", s) && !parse_upscaling("dlss-", s));
    CHECK(parse_frame_gen("high", s) && s.frame_gen == FrameGen::VfxFrameGen && s.frame_gen_quality == FrameGenQuality::High);
    CHECK(!parse_frame_gen("x", s));
    CHECK(parse_cleanup("denoise", s) && s.cleanup == Cleanup::Denoise);
    CHECK(parse_cleanup("deblur-high", s) && s.cleanup == Cleanup::Deblur && s.cleanup_strength == SuperResQuality::High);
    CHECK(parse_cleanup("off", s) && s.cleanup == Cleanup::Off);
    CHECK(!parse_cleanup("denoise-max", s) && !parse_cleanup("sharpen", s) && !parse_cleanup("off-high", s));
    // NIS output is capped at 2x the source.
    CHECK((upscale_target({960, 540}, {3840, 2160}, 2.0) == Size{1920, 1080}));
    CHECK((upscale_target({960, 540}, {3840, 2160}) == Size{3840, 2160}));
}

void test_stride()
{
    const double p = 1.0 / 165;  // 6.06 ms
    CHECK(frame_gen_stride(1.4, p, 4) == 1);   // MEDIUM: every refresh
    CHECK(frame_gen_stride(5.7, p, 4) == 1);   // just under 95 %
    CHECK(frame_gen_stride(5.8, p, 4) == 2);
    CHECK(frame_gen_stride(10.1, p, 4) == 2);  // HIGH at 1080p: every 2nd refresh
    CHECK(frame_gen_stride(12.0, p, 4) == 3);
    CHECK(frame_gen_stride(80.0, p, 4) == 4);  // capped
    CHECK(frame_gen_stride(10.0, 1.0 / 60, 4) == 1);  // 60 Hz display: 16.7 ms per refresh
    CHECK(frame_gen_stride(0, p, 4) == 1);
}

void test_av_sync()
{
    using audio::sync_ratio;
    CHECK(near(sync_ratio(0), 1.0));
    CHECK(sync_ratio(0.004) < 1.0);   // sound early: play slower
    CHECK(sync_ratio(-0.004) > 1.0);  // sound late: play faster
    CHECK(near(sync_ratio(0.004), 0.998));
    CHECK(near(sync_ratio(0.05), 0.99));    // capped at 1 %
    CHECK(near(sync_ratio(-0.05), 1.01));
    CHECK(sync_ratio(0.09) == 0);           // too far: resync
    CHECK(sync_ratio(-0.2) == 0);
    // Closed loop: a constant 0.05 % clock difference between the sound card and the display settles near 0.
    double err = 0.02, ratio = 1.0;
    bool resynced = false;
    for (int i = 0; i < 165 * 20; ++i) {  // 20 s of refreshes
        err += (ratio - 1.0005) * (1.0 / 165);  // sound gains (ratio - drift) seconds per second
        ratio = sync_ratio(err);
        resynced |= ratio == 0;
    }
    CHECK(!resynced);
    CHECK(std::fabs(err) < 0.002);
}

} // namespace

int main()
{
    test_layout();
    test_pick();
    test_clock();
    test_scene_cut();
    test_queue();
    test_settings();
    test_av_sync();
    test_stride();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
