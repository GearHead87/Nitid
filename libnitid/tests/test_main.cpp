// libnitid unit tests: frame layout, frame selection, display clock, scene cuts, queue.
// Tiny self-contained harness (no test framework dependency).
#include "core/bounded_queue.hpp"
#include "input/scene_cut.hpp"
#include "pipeline/frame_clock.hpp"
#include "pipeline/frame_layout.hpp"

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

} // namespace

int main()
{
    test_layout();
    test_pick();
    test_clock();
    test_scene_cut();
    test_queue();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
