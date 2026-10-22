#pragma once
#include <vector>
#include <numeric>
#include <cmath>

class FeatureExtractor {
private:
    size_t window_size;
    std::vector<double> timestamps_ms;

public:
    explicit FeatureExtractor(size_t size = 20) : window_size(size) {
        timestamps_ms.reserve(size);
    }

    void add_sample(double delta_ms) {
        if (timestamps_ms.size() >= window_size) {
            timestamps_ms.erase(timestamps_ms.begin());
        }
        timestamps_ms.push_back(delta_ms);
    }

    double get_time_variance() const {
        if (timestamps_ms.size() < 2) return 100.0; // High variance default for new users

        double sum = std::accumulate(timestamps_ms.begin(), timestamps_ms.end(), 0.0);
        double mean = sum / timestamps_ms.size();

        double sq_sum = 0.0;
        for (double val : timestamps_ms) {
            sq_sum += (val - mean) * (val - mean);
        }
        return sq_sum / timestamps_ms.size();
    }
};
