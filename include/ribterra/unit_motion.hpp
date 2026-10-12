#pragma once

#include "ribterra/world_grid.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace ribterra {

struct MotionPosition {
    float x{}, y{};
    WorldLayer layer{WorldLayer::Surface};
};

// Presentation time deliberately trails authority. Packet arrivals only add samples;
// they never directly advance a unit's screen position. On starvation we hold at
// the newest known position instead of predicting a route through unknown terrain.
class MotionClock {
  public:
    bool update(std::uint64_t tick, double seconds, int speed, bool paused, bool remote) {
        const double rate = 20.0 * std::clamp(speed, 1, 4);
        // A resumed/background client can miss seconds of simulation. Rebase
        // instead of replaying stale history for minutes at the catch-up rate.
        if (!initialized_ || tick < latest_ || seconds < previous_ ||
            double(tick) - shown_ > rate * 2.0) {
            initialized_ = true;
            latest_ = tick;
            shown_ = double(tick);
            previous_ = received_ = seconds;
            return true;
        }
        const double dt = std::clamp(seconds - previous_, 0.0, .25);
        previous_ = seconds;
        if (tick != latest_) {
            latest_ = tick;
            received_ = seconds;
        }
        if (paused) {
            received_ = seconds;
            return false;
        }
        const double delay = remote ? .25 : .05;
        const double target = std::min(double(latest_),
            double(latest_) + (seconds - received_ - delay) * rate);
        // Correct delivery jitter gradually; never rewind or snap on an arrival.
        shown_ += std::clamp(target - shown_, 0.0, dt * rate * 1.1);
        return false;
    }
    double tick() const { return shown_; }
  private:
    bool initialized_{};
    std::uint64_t latest_{};
    double shown_{}, previous_{}, received_{};
};

class UnitMotion {
  public:
    std::uint64_t revision{};
    // Returns true for a discontinuity, so the caller also resets the walk cycle.
    bool add(MotionPosition position, std::uint64_t tick) {
        bool discontinuity = false;
        if (!samples_.empty()) {
            const auto &last = samples_.back();
            const double elapsed = tick >= last.tick ? double(tick - last.tick) : 0;
            const float distance = std::abs(position.x - last.position.x) +
                                   std::abs(position.y - last.position.y);
            // Residents move .125/tick; desire paths are visual and never alter speed.
            // A multi-tick network update is ordinary walking, not a teleport.
            discontinuity = tick < last.tick || position.layer != last.position.layer ||
                            distance > std::max(2.0, elapsed * .25 + 1.0);
            if (discontinuity) samples_.clear();
            else if (tick == last.tick) {
                samples_.back().position = position;
                return false;
            }
        }
        samples_.push_back({tick, position});
        while (samples_.size() > 32) samples_.pop_front();
        return discontinuity;
    }
    MotionPosition sample(double tick) const {
        if (samples_.empty()) return {};
        if (tick <= double(samples_.front().tick)) return samples_.front().position;
        for (std::size_t i = 1; i < samples_.size(); ++i) {
            const auto &next = samples_[i];
            if (tick > double(next.tick)) continue;
            const auto &prior = samples_[i - 1];
            const float alpha = float((tick - double(prior.tick)) / double(next.tick - prior.tick));
            return {std::lerp(prior.position.x, next.position.x, alpha),
                    std::lerp(prior.position.y, next.position.y, alpha), next.position.layer};
        }
        return samples_.back().position;
    }
  private:
    struct Sample { std::uint64_t tick; MotionPosition position; };
    std::deque<Sample> samples_;
};

} // namespace ribterra
