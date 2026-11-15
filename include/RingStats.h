#pragma once
#include <array>
#include <cmath>
#include <cstddef>

// Fixed-size circular buffer of doubles with O(1) insertion and simple statistics.
// No heap allocation: the storage is a std::array inside the object.
template <std::size_t N>
class RingStats {
private:
    std::array<double, N> buf_{};
    std::size_t head_ = 0;   // next slot to write
    std::size_t count_ = 0;  // number of valid samples (max N)

public:
    void push(double v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % N;   // wrap around: oldest value gets overwritten
        if (count_ < N) ++count_;
    }

    std::size_t size() const { return count_; }

    double mean() const {
        if (count_ == 0) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < count_; ++i) sum += buf_[i];
        return sum / static_cast<double>(count_);
    }

    // Population variance. Order of samples does not matter, so we only
    // look at the first count_ slots.
    double variance() const {
        if (count_ < 2) return 0.0;
        const double m = mean();
        double sq = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const double d = buf_[i] - m;
            sq += d * d;
        }
        return sq / static_cast<double>(count_);
    }

    // Coefficient of variation = standard deviation / mean.
    // Scale-free: a client sending every 50 ms and one sending every 5 s are
    // judged on how regular they are, not on how fast they are.
    double cv() const {
        const double m = mean();
        if (m < 1e-9) return 0.0;
        return std::sqrt(variance()) / m;
    }
};
