#include "ribterra/frame_timing.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ribterra::render;

namespace {

void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void summaries_use_nearest_rank() {
    FrameTimingWindow timings;
    for (int value = 1; value <= 100; ++value) timings.add(static_cast<double>(value));
    const auto result = timings.summary(90.0);
    check(result.retained_samples == 100 && result.total_samples == 100, "Timing sample counts are wrong");
    check(result.mean_ms == 50.5, "Timing mean is wrong");
    check(result.p50_ms == 50.0 && result.p95_ms == 95.0 && result.p99_ms == 99.0,
          "Nearest-rank percentiles are wrong");
    check(result.max_ms == 100.0 && result.over_budget == 10, "Timing maximum or budget count is wrong");
}

void ring_overwrites_and_clear_resets() {
    FrameTimingWindow timings;
    for (std::size_t value = 0; value < FrameTimingWindow::capacity + 3; ++value)
        timings.add(static_cast<double>(value));
    const auto result = timings.summary(8192.0);
    check(result.retained_samples == FrameTimingWindow::capacity,
          "Timing window did not retain its bounded capacity");
    check(result.total_samples == FrameTimingWindow::capacity + 3,
          "Timing window lost its cumulative count");
    check(result.p50_ms == 4098.0 && result.max_ms == 8194.0,
          "Timing window did not overwrite the oldest samples");
    check(result.over_budget == 2, "Timing window budget count ignored overwritten samples");
    timings.clear();
    const auto cleared = timings.summary();
    check(cleared.retained_samples == 0 && cleared.total_samples == 0 && cleared.mean_ms == 0.0,
          "Timing clear did not reset the window");
}

void invalid_values_are_rejected() {
    FrameTimingWindow timings;
    timings.add(0.0);
    timings.add(-1.0);
    timings.add(std::numeric_limits<double>::infinity());
    timings.add(std::numeric_limits<double>::quiet_NaN());
    const auto result = timings.summary(0.0);
    check(result.retained_samples == 1 && result.total_samples == 1 && result.max_ms == 0.0,
          "Invalid durations were accepted or zero was rejected");
    check(result.over_budget == 0, "A frame equal to the budget was counted over budget");
    bool rejected = false;
    try { (void)timings.summary(-1.0); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Negative budget was accepted");
    rejected = false;
    try { (void)timings.summary(std::numeric_limits<double>::infinity()); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Infinite budget was accepted");
}

void uncapped_and_fractional_limits() {
    FrameLimiter limiter;
    limiter.reset(100);
    check(limiter.delay_ns(200) == 0, "Uncapped limiter requested a delay");
    limiter.set_limit(120);
    limiter.reset(0);
    std::uint64_t now = 0;
    for (int frame = 1; frame <= 120; ++frame) {
        now += limiter.delay_ns(now);
        check(now == (static_cast<std::uint64_t>(frame) * 1'000'000'000ULL) / 120,
              "120 Hz limiter accumulated fractional drift");
    }
    check(now == 1'000'000'000ULL, "120 Hz limiter did not land on one second");
}

void late_frames_skip_catchup() {
    FrameLimiter limiter;
    limiter.set_limit(100);
    limiter.reset(0);
    check(limiter.delay_ns(35'000'000) == 0, "Late frame requested catch-up sleep");
    check(limiter.delay_ns(36'000'000) == 4'000'000, "Late frame did not advance to the next absolute deadline");
}

void late_frames_keep_fractional_deadlines() {
    FrameLimiter limiter;
    limiter.set_limit(60);
    limiter.reset(0);
    check(limiter.delay_ns(49'999'998) == 0, "60 Hz late frame requested catch-up sleep");
    check(limiter.delay_ns(49'999'999) == 1,
          "60 Hz late skip passed the next fractional deadline");

    limiter.set_limit(120);
    limiter.reset(0);
    check(limiter.delay_ns(24'999'999) == 0, "120 Hz late frame requested catch-up sleep");
    check(limiter.delay_ns(24'999'999) == 1,
          "120 Hz late skip passed the next fractional deadline");
}

void cap_changes_reset_scheduling() {
    FrameLimiter limiter;
    limiter.set_limit(60);
    limiter.reset(1'000);
    check(limiter.delay_ns(1'000) == 16'666'666, "60 Hz initial delay is wrong");
    limiter.set_limit(0);
    check(limiter.delay_ns(900) == 0, "Switching to uncapped retained scheduling");
    limiter.set_limit(1000);
    check(limiter.delay_ns(5'000) == 1'000'000, "New cap did not start a fresh schedule");
    bool rejected = false;
    try { limiter.set_limit(1001); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Frame limit above 1000 was accepted");
}

void discontinuities_are_safe() {
    FrameLimiter limiter;
    limiter.set_limit(1000);
    limiter.reset(std::numeric_limits<std::uint64_t>::max() - 500'000);
    check(limiter.delay_ns(std::numeric_limits<std::uint64_t>::max() - 500'000) == 500'000,
          "Deadline overflow was not saturated safely");
    check(limiter.delay_ns(std::numeric_limits<std::uint64_t>::max()) == 0,
          "Saturated deadline requested a delay");
    check(limiter.delay_ns(10) == 1'000'000, "Clock rollback did not reset scheduling");
}

} // namespace

int main() {
    try {
        summaries_use_nearest_rank();
        ring_overwrites_and_clear_resets();
        invalid_values_are_rejected();
        uncapped_and_fractional_limits();
        late_frames_skip_catchup();
        late_frames_keep_fractional_deadlines();
        cap_changes_reset_scheduling();
        discontinuities_are_safe();
        std::cout << "Frame timing window and absolute-deadline limiter tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
