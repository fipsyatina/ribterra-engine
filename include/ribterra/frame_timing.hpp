#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ribterra::render {

struct TimingSummary {
    std::size_t retained_samples{};
    std::uint64_t total_samples{};
    double mean_ms{};
    double p50_ms{};
    double p95_ms{};
    double p99_ms{};
    double max_ms{};
    std::uint64_t over_budget{};
};

class FrameTimingWindow {
public:
    static constexpr std::size_t capacity = 8192;

    void add(double milliseconds) noexcept {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) return;
        samples_[next_] = milliseconds;
        next_ = (next_ + 1) % capacity;
        if (retained_ < capacity) ++retained_;
        if (total_ != std::numeric_limits<std::uint64_t>::max()) ++total_;
    }

    [[nodiscard]] TimingSummary summary(double budget_ms = 1000.0 / 120.0) const {
        if (!std::isfinite(budget_ms) || budget_ms < 0.0)
            throw std::invalid_argument("frame timing budget must be finite and non-negative");

        TimingSummary result;
        result.retained_samples = retained_;
        result.total_samples = total_;
        if (retained_ == 0) return result;

        std::array<double, capacity> sorted{};
        double total_ms = 0.0;
        for (std::size_t i = 0; i < retained_; ++i) {
            sorted[i] = samples_[i];
            total_ms += samples_[i];
            if (samples_[i] > budget_ms) ++result.over_budget;
        }
        std::sort(sorted.begin(), sorted.begin() + retained_);
        result.mean_ms = total_ms / static_cast<double>(retained_);
        result.p50_ms = nearest_rank(sorted, retained_, 50);
        result.p95_ms = nearest_rank(sorted, retained_, 95);
        result.p99_ms = nearest_rank(sorted, retained_, 99);
        result.max_ms = sorted[retained_ - 1];
        return result;
    }

    void clear() noexcept {
        retained_ = 0;
        next_ = 0;
        total_ = 0;
    }

private:
    // Nearest-rank percentile: rank = ceil(percent * N / 100), using the
    // one-based rank clamped to [1, N]. This intentionally does not interpolate.
    static double nearest_rank(const std::array<double, capacity>& sorted,
                               std::size_t count, std::size_t percent) noexcept {
        const std::size_t rank = (percent * count + 99) / 100;
        return sorted[rank - 1];
    }

    std::array<double, capacity> samples_{};
    std::size_t retained_{};
    std::size_t next_{};
    std::uint64_t total_{};
};

class FrameLimiter {
public:
    void set_limit(std::uint32_t fps) {
        if (fps > 1000) throw std::invalid_argument("frame limit must be in [0, 1000]");
        fps_ = fps;
        scheduled_ = false;
        last_now_valid_ = false;
        remainder_accumulator_ = 0;
        if (fps_ == 0) {
            period_ns_ = 0;
            remainder_ns_ = 0;
        } else {
            period_ns_ = nanoseconds_per_second / fps_;
            remainder_ns_ = nanoseconds_per_second % fps_;
        }
    }

    // start_ns and subsequent now_ns values are assumed to be readings from the
    // same monotonic clock. A rollback is treated as a clock discontinuity and
    // starts a fresh schedule at the new reading.
    void reset(std::uint64_t start_ns) noexcept {
        remainder_accumulator_ = 0;
        last_now_ = start_ns;
        last_now_valid_ = fps_ != 0;
        scheduled_ = fps_ != 0;
        next_deadline_ = start_ns;
        if (scheduled_) advance_deadline(1);
    }

    [[nodiscard]] std::uint64_t delay_ns(std::uint64_t now_ns) noexcept {
        if (fps_ == 0) return 0;
        if (!scheduled_ || (last_now_valid_ && now_ns < last_now_)) reset(now_ns);
        last_now_ = now_ns;
        last_now_valid_ = true;

        if (now_ns < next_deadline_) {
            const std::uint64_t delay = next_deadline_ - now_ns;
            advance_deadline(1);
            return delay;
        }

        if (next_deadline_ != std::numeric_limits<std::uint64_t>::max()) {
            const std::uint64_t behind = now_ns - next_deadline_;
            std::uint64_t first = 1;
            std::uint64_t last = behind / period_ns_ + 1;
            while (first < last) {
                const std::uint64_t middle = first + (last - first) / 2;
                if (deadline_increment(middle) > behind) last = middle;
                else first = middle + 1;
            }
            advance_deadline(first);
        }
        return 0;
    }

private:
    static constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

    static constexpr std::uint64_t saturating_add(std::uint64_t left,
                                                   std::uint64_t right) noexcept {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return right > maximum - left ? maximum : left + right;
    }

    static constexpr std::uint64_t saturating_multiply(std::uint64_t left,
                                                        std::uint64_t right) noexcept {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return left != 0 && right > maximum / left ? maximum : left * right;
    }

    [[nodiscard]] std::uint64_t deadline_increment(std::uint64_t steps) const noexcept {
        const std::uint64_t cycles = steps / fps_;
        const std::uint64_t tail = steps % fps_;
        const std::uint64_t fractional = remainder_accumulator_ + tail * remainder_ns_;
        std::uint64_t increment = saturating_multiply(steps, period_ns_);
        increment = saturating_add(increment, saturating_multiply(cycles, remainder_ns_));
        increment = saturating_add(increment, fractional / fps_);
        return increment;
    }

    void advance_deadline(std::uint64_t steps) noexcept {
        const std::uint64_t increment = deadline_increment(steps);
        const std::uint64_t tail = steps % fps_;
        remainder_accumulator_ = (remainder_accumulator_ + tail * remainder_ns_) % fps_;
        next_deadline_ = saturating_add(next_deadline_, increment);
    }

    std::uint32_t fps_{};
    std::uint64_t period_ns_{};
    std::uint64_t remainder_ns_{};
    std::uint64_t remainder_accumulator_{};
    std::uint64_t next_deadline_{};
    std::uint64_t last_now_{};
    bool scheduled_{};
    bool last_now_valid_{};
};

} // namespace ribterra::render
