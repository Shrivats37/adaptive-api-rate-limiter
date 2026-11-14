#pragma once
#include <array>
#include <numeric>
#include <cmath>

class FeatureExtractor {
private:
    static constexpr std::size_t WINDOW_SIZE = 20;
    std::array<double, WINDOW_SIZE> samples{};
    std::size_t head = 0;   // next slot to write
    std::size_t count = 0; 

public:
     void add_sample(double delta_ms) {
        samples[head] = delta_ms;
        head = (head + 1) % WINDOW_SIZE;
        if (count < WINDOW_SIZE) ++count;
    }

    double get_time_variance() const {
        if (count < 2) return 100.0; // high variance default for new clients

        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) sum += samples[i];
        double mean = sum / count;

        double sq_sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            double d = samples[i] - mean;
            sq_sum += d * d;
        }
        return sq_sum / count;
    }
};
