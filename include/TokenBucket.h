#pragma once
#include <chrono>
#include <mutex>
#include <algorithm>

class TokenBucket {
private:
    double capacity;
    double refill_rate;
    double tokens;
    std::chrono::steady_clock::time_point last_refill;
    std::mutex mtx;

    void refill() {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last_refill;
        last_refill = now;
        tokens = std::min(capacity, tokens + elapsed.count() * refill_rate);
    }

public:
    TokenBucket(double cap, double rate) 
        : capacity(cap), refill_rate(rate), tokens(cap), last_refill(std::chrono::steady_clock::now()) {}

    bool consume(double tokens_to_consume = 1.0) {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        if (tokens >= tokens_to_consume) {
            tokens -= tokens_to_consume;
            return true;
        }
        return false;
    }

    void adapt_parameters(double risk_score) {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        if (risk_score > 0.7) {
            capacity = 5.0;
            refill_rate = 0.5;
        } else if (risk_score > 0.4) {
            capacity = 20.0;
            refill_rate = 2.0;
        } else {
            capacity = 100.0;
            refill_rate = 10.0;
        }
        tokens = std::min(tokens, capacity);
    }
};
