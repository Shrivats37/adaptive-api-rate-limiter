#pragma once
#include <chrono>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <cstdint>

class TokenBucket {
private:
    double capacity;
    double refill_rate;
    double tokens;
    std::chrono::steady_clock::time_point last_refill;
    std::mutex mtx;
    std::atomic<std::uint64_t> allowed_count{0};
    std::atomic<std::uint64_t> blocked_count{0};

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
        bool ok;
    {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        ok = tokens >= tokens_to_consume;
        if (ok) tokens -= tokens_to_consume;
    }   // lock yahan khatam, mutex release ho gaya

    if (ok) allowed_count.fetch_add(1, std::memory_order_relaxed);
    else    blocked_count.fetch_add(1, std::memory_order_relaxed);
    return ok;
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

    std::uint64_t allowed() const { return allowed_count.load(std::memory_order_relaxed); }
    std::uint64_t blocked() const { return blocked_count.load(std::memory_order_relaxed); }
};
