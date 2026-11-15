#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "RingStats.h"

// The three behaviour signals fed to the risk model.
struct Features {
    double gap_cv;              // regularity of time gaps between requests (low = machine-like)
    double payload_cv;          // regularity of payload sizes (low = machine-like)
    double top_endpoint_share;  // share of requests hitting the most-used endpoint (high = bot-like)
};

// Sliding window (last 20 requests) of one client's behaviour.
class FeatureExtractor {
public:
    static constexpr std::size_t WINDOW_SIZE = 20;
    static constexpr std::size_t MIN_SAMPLES = 5;   // below this we do not score the client (cold start)

private:
    static constexpr std::size_t EP_BUCKETS = 32;

    RingStats<WINDOW_SIZE> gaps_;
    RingStats<WINDOW_SIZE> payloads_;
    std::array<std::uint8_t, WINDOW_SIZE> endpoints_{};
    std::size_t ep_head_ = 0;
    std::size_t ep_count_ = 0;

public:
    // Gap-only insertion, kept for the simple unit test of the ring buffer.
    void add_sample(double delta_ms) { gaps_.push(delta_ms); }

    // Full insertion used in the real pipeline.
    void add_request(double gap_ms, double payload_bytes, unsigned endpoint) {
        gaps_.push(gap_ms);
        payloads_.push(payload_bytes);
        endpoints_[ep_head_] = static_cast<std::uint8_t>(endpoint % EP_BUCKETS);
        ep_head_ = (ep_head_ + 1) % WINDOW_SIZE;
        if (ep_count_ < WINDOW_SIZE) ++ep_count_;
    }

    // Variance of time gaps (ms^2). New clients get a high default.
    double get_time_variance() const {
        if (gaps_.size() < 2) return 100.0;
        return gaps_.variance();
    }

    bool ready() const { return gaps_.size() >= MIN_SAMPLES; }

    Features features() const {
        Features f{};
        f.gap_cv = gaps_.cv();
        f.payload_cv = payloads_.cv();
        if (ep_count_ > 0) {
            std::array<unsigned, EP_BUCKETS> counts{};
            unsigned best = 0;
            for (std::size_t i = 0; i < ep_count_; ++i) {
                unsigned c = ++counts[endpoints_[i]];
                if (c > best) best = c;
            }
            f.top_endpoint_share = static_cast<double>(best) / static_cast<double>(ep_count_);
        } else {
            f.top_endpoint_share = 0.0;
        }
        return f;
    }
};
