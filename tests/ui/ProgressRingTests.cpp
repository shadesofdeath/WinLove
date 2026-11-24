// D-142: the progress ring — its motion as a function of time (no jumps, always a dash, determinate from
// 12 o'clock), the still pose and --anim-time, the frames it asks for (only while turning and drawn), and
// a crisp ring at every scale.
#include "support/TestGraphics.h"
#include "ui/anim/Tween.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/widget/Host.h"
#include "ui/widgets/ProgressRing.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <vector>

using namespace wl;
using namespace wl::ui;

namespace {

// Motion as a user has it; Windows' own "Show animations" may still be off on the machine running the tests.
struct Motion {
    explicit Motion(bool reduced) {
        forceInstantMotion(false);
        setReducedMotionForced(reduced);
        setRingTime(std::nullopt);
    }
    ~Motion() {
        setReducedMotionForced(false);
        setRingTime(std::nullopt);
    }
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
};

float angleStep(float a, float b) {
    float d = std::fmod(std::abs(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

class OneChild : public Widget {
public:
    OneChild(std::unique_ptr<Widget> child, RectF rect) : m_rect(rect) { addChild(std::move(child)); }
    void layout() override {
        if (!children().empty()) {
            children()[0]->setBounds(m_rect);
        }
    }

private:
    RectF m_rect;
};

struct Frames {
    int requested = 0;
    std::optional<UINT> timerMs;
};

} // namespace

TEST_CASE("D-142: the indeterminate arc grows and shrinks while it turns, without a jump") {
    const float cycle = tokens::motion::ringCycleMs;
    CHECK(ringPose(0).sweepDegrees == doctest::Approx(20.0f));                 // a dash, never a dot
    CHECK(ringPose(cycle / 2).sweepDegrees == doctest::Approx(270.0f));        // the longest arc
    CHECK(ringPose(cycle).sweepDegrees == doctest::Approx(20.0f).epsilon(0.01));
    float minSweep = 1000;
    float maxSweep = 0;
    RingPose last = ringPose(0);
    float worstStart = 0;
    float worstSweep = 0;
    for (int ms = 1; ms <= 20000; ++ms) {
        const RingPose p = ringPose(ms);
        minSweep = std::min(minSweep, p.sweepDegrees);
        maxSweep = std::max(maxSweep, p.sweepDegrees);
        worstStart = std::max(worstStart, angleStep(p.startDegrees, last.startDegrees));
        worstSweep = std::max(worstSweep, std::abs(p.sweepDegrees - last.sweepDegrees));
        last = p;
    }
    CHECK(minSweep >= 19.9f);
    CHECK(maxSweep <= 270.1f);
    // One millisecond moves the tail and the length by little: the motion is smooth at any frame rate.
    CHECK(worstStart < 1.5f);
    CHECK(worstSweep < 1.5f);
    // The same time is the same pose (offscreen frames are reproducible), also after hours of a job.
    CHECK(ringPose(1234.5).startDegrees == ringPose(1234.5).startDegrees);
    const RingPose late = ringPose(36.0e6 + 250.0);
    CHECK(std::isfinite(late.startDegrees));
    CHECK(late.startDegrees >= -90.0f);
    CHECK(late.startDegrees < 270.0f);
    CHECK(ringPose(-50).sweepDegrees == ringPose(0).sweepDegrees); // a clock read before the start
}

TEST_CASE("D-142: the determinate arc and the still arc start at 12 o'clock") {
    CHECK(ringValuePose(0.25f).startDegrees == doctest::Approx(-90.0f));
    CHECK(ringValuePose(0.25f).sweepDegrees == doctest::Approx(90.0f));
    CHECK(ringValuePose(1.5f).sweepDegrees == doctest::Approx(360.0f));
    CHECK(ringValuePose(-1.0f).sweepDegrees == doctest::Approx(0.0f));
    CHECK(ringStillPose().startDegrees == doctest::Approx(-90.0f));
    CHECK(ringStillPose().sweepDegrees == doctest::Approx(270.0f));
}

TEST_CASE("D-142: a strip's ring clock: the still pose when motion is reduced, --anim-time's pose, its frame pace") {
    {
        Motion motion(/*reduced=*/true);
        RingClock clock;
        clock.start(1000);
        CHECK(clock.running());
        CHECK_FALSE(clock.turning());
        CHECK(clock.tickMs(250.0f) == 250.0f); // no ring frames: the strip's own pace
        CHECK(clock.pose(1700).sweepDegrees == ringStillPose().sweepDegrees);
        setRingTime(400.0);
        CHECK(clock.pose(99999).startDegrees == ringPose(400.0).startDegrees); // a render frame at 400 ms
        CHECK(clock.pose(99999).sweepDegrees == ringPose(400.0).sweepDegrees);
    }
    {
        Motion motion(/*reduced=*/false);
        RingClock clock;
        CHECK_FALSE(clock.running());
        clock.start(1000);
        clock.start(5000); // a second job on the same strip keeps the phase
        if (!reducedMotion()) {
            CHECK(clock.turning());
            CHECK(clock.tickMs(250.0f) == tokens::motion::ringFrameMs);
            CHECK(clock.pose(1700).sweepDegrees == ringPose(700).sweepDegrees);
        }
        clock.stop();
        CHECK_FALSE(clock.running());
        CHECK(clock.tickMs(250.0f) == 250.0f);
    }
}

TEST_CASE("D-142: a ring widget asks for frames only while it turns and is drawn") {
    Motion motion(/*reduced=*/false);
    if (reducedMotion()) {
        return; // Windows has animations off here: the ring is still (the case below)
    }
    Frames frames;
    auto ringOwned = std::make_unique<ProgressRing>(tokens::size::ring);
    ProgressRing* ring = ringOwned.get();
    Host host(HostServices{[&] { ++frames.requested; }, [&](UINT, UINT ms) { frames.timerMs = ms; }, [](UINT) {},
                           test::graphics().text.get()});
    host.setRoot(std::make_unique<OneChild>(std::move(ringOwned), RectF{10, 10, 16, 16}));
    host.layout({100, 60});
    auto target = OffscreenTarget::create(*test::graphics().device, {100, 60}, 1.0f);
    REQUIRE(target.has_value());
    auto frame = [&] {
        frames.timerMs.reset();
        Canvas canvas((*target)->beginDraw(), ThemeKind::Dark, 1.0f, *test::graphics().text, *test::graphics().icons);
        canvas.clear(tokens::Color::BgPanel);
        host.paint(canvas);
        REQUIRE((*target)->endDraw().has_value());
    };

    frame();
    CHECK_FALSE(ring->turning()); // idle: no frames
    CHECK_FALSE(frames.timerMs.has_value());
    ring->setRunning(true);
    CHECK(ring->turning());
    frame();
    CHECK(ring->turning());
    REQUIRE(frames.timerMs.has_value());
    CHECK(*frames.timerMs == static_cast<UINT>(tokens::motion::ringFrameMs)); // its pace, not the display's
    // Hidden (its page left, scrolled away): one frame later it stops asking.
    ring->setVisible(false);
    frame();
    frame();
    CHECK_FALSE(ring->turning());
    CHECK_FALSE(frames.timerMs.has_value());
    // Drawn again: it goes on by itself.
    ring->setVisible(true);
    frame();
    CHECK(ring->turning());
    // Stopped: no more frames.
    ring->setRunning(false);
    frame();
    frame();
    CHECK_FALSE(ring->turning());
    CHECK_FALSE(frames.timerMs.has_value());
    // Reduced motion: a still ring, no frames.
    setReducedMotionForced(true);
    ring->setRunning(true);
    frame();
    CHECK_FALSE(ring->turning());
    CHECK_FALSE(frames.timerMs.has_value());
}

TEST_CASE("D-142: the ring is crisp at every scale (whole-pixel stroke on the pixel grid)") {
    for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        CAPTURE(scale);
        auto target = OffscreenTarget::create(*test::graphics().device, {40, 40}, scale);
        REQUIRE(target.has_value());
        {
            Canvas canvas((*target)->beginDraw(), ThemeKind::Dark, scale, *test::graphics().text, *test::graphics().icons);
            canvas.clear(tokens::Color::BgPanel);
            paintRing(canvas, {10, 10, tokens::size::ringLarge, tokens::size::ringLarge}, ringValuePose(1.0f));
        }
        REQUIRE((*target)->endDraw().has_value());
        const auto pixels = (*target)->readPixels();
        REQUIRE(pixels.has_value());
        const UINT w = (*target)->widthPx();
        const Rgba ink = color(ThemeKind::Dark, tokens::Color::AccentText);
        const Rgba bg = color(ThemeKind::Dark, tokens::Color::BgPanel);
        // The left edge on the middle row: the first pixel off the background is (almost) all ink, not half.
        const UINT y = static_cast<UINT>(std::lround((10 + tokens::size::ringLarge / 2) * scale)) - 1;
        const UINT x0 = static_cast<UINT>(std::lround(10 * scale));
        const std::uint32_t edge = (*pixels)[static_cast<std::size_t>(y) * w + x0];
        const std::uint32_t before = (*pixels)[static_cast<std::size_t>(y) * w + x0 - 1];
        CHECK(before == colorArgb(ThemeKind::Dark, tokens::Color::BgPanel));
        const float green = static_cast<float>((edge >> 8) & 0xFF) / 255.0f;
        const float coverage = (green - bg.g) / (ink.g - bg.g);
        CHECK(coverage > 0.85f);
        // The middle is empty.
        const UINT mid = static_cast<UINT>(std::lround(20 * scale));
        CHECK((*pixels)[static_cast<std::size_t>(mid) * w + mid] == colorArgb(ThemeKind::Dark, tokens::Color::BgPanel));
    }
}

// By hand, in Release (docs/PERF-BASELINE.md's way): build\x64-release\bin\wl_tests.exe -tc="perf: ring*" --no-skip -s
// What one ring adds to a frame (an arc geometry made and stroked, a track), and what the D-143 rules cost
// for one colour (the picker works them out on every drag step).
TEST_CASE("perf: ring paint and accent rules" * doctest::skip()) {
    auto target = OffscreenTarget::create(*test::graphics().device, {200, 200}, 1.5f);
    REQUIRE(target.has_value());
    constexpr int kRings = 2000;
    const auto started = std::chrono::steady_clock::now();
    {
        Canvas canvas((*target)->beginDraw(), ThemeKind::Dark, 1.5f, *test::graphics().text, *test::graphics().icons);
        canvas.clear(tokens::Color::BgPanel);
        for (int i = 0; i < kRings; ++i) {
            paintRing(canvas, {20, 20, tokens::size::ringLarge, tokens::size::ringLarge}, ringPose(i * 25.0));
        }
    }
    REQUIRE((*target)->endDraw().has_value());
    const double ringUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() / kRings;
    constexpr int kColours = 300;
    const auto derived = std::chrono::steady_clock::now();
    std::uint32_t sink = 0;
    for (int i = 0; i < kColours; ++i) {
        sink ^= deriveAccent(fromHsv({static_cast<float>(i), 0.8f, 0.9f})).dark.text;
    }
    const double deriveUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - derived).count() / kColours;
    MESSAGE(std::format("ring paint (record + draw, offscreen GPU): {:.1f} us   deriveAccent: {:.1f} us ({:X})", ringUs, deriveUs, sink));
}
