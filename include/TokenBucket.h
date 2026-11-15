#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

using Clock = std::chrono::steady_clock;

// Thread-safe token bucket whose capacity and refill rate can be changed at runtime.
// The *_at / time-parameter versions let tests and benchmarks use a virtual clock.
class TokenBucket {
private:
    double capacity;
    double refill_rate;   // tokens per second
    double tokens;
    Clock::time_point last_refill;
    std::mutex mtx;
    std::atomic<std::uint64_t> allowed_count{0};
    std::atomic<std::uint64_t> blocked_count{0};

    // Must be called with mtx held.
    void refill(Clock::time_point now) {
        std::chrono::duration<double> elapsed = now - last_refill;
        if (elapsed.count() <= 0.0) return;   // out-of-order timestamp: never move time backwards
        last_refill = now;
        tokens = std::min(capacity, tokens + elapsed.count() * refill_rate);
    }

public:
    TokenBucket(double cap, double rate, Clock::time_point start = Clock::now())
        : capacity(cap), refill_rate(rate), tokens(cap), last_refill(start) {}

    bool consume(double n = 1.0) { return consume_at(Clock::now(), n); }

    bool consume_at(Clock::time_point now, double n = 1.0) {
        bool ok;
        {
            std::lock_guard<std::mutex> lock(mtx);   // critical section: token arithmetic only
            refill(now);
            ok = tokens >= n;
            if (ok) tokens -= n;
        }
        // Metrics are updated outside the lock with atomics.
        if (ok) allowed_count.fetch_add(1, std::memory_order_relaxed);
        else    blocked_count.fetch_add(1, std::memory_order_relaxed);
        return ok;
    }

    void adapt_parameters(double risk_score, Clock::time_point now = Clock::now()) {
        std::lock_guard<std::mutex> lock(mtx);
        refill(now);   // apply the old rate to the time already passed
        if (risk_score > 0.7) {
            capacity = 5.0;   refill_rate = 0.5;
        } else if (risk_score > 0.4) {
            capacity = 20.0;  refill_rate = 2.0;
        } else {
            capacity = 100.0; refill_rate = 10.0;
        }
        tokens = std::min(tokens, capacity);
    }

    std::uint64_t allowed() const { return allowed_count.load(std::memory_order_relaxed); }
    std::uint64_t blocked() const { return blocked_count.load(std::memory_order_relaxed); }
};
