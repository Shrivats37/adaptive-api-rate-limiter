#pragma once
#include <cmath>
#include <vector>

class RiskEngine {
private:
    std::vector<double> weights = {-1.5, -0.8, 2.2};
    double bias = 0.5;

    double sigmoid(double z) const {
        return 1.0 / (1.0 + std::exp(-z));
    }

public:
    double evaluate(double time_variance, double payload_variance, double header_entropy) {
        std::vector<double> features = {time_variance, payload_variance, header_entropy};
        double z = bias;
        for (size_t i = 0; i < features.size(); ++i) {
            z += weights[i] * features[i];
        }
        return sigmoid(z); // Returns probability R between 0.0 (Human) and 1.0 (Bot)
    }
};
