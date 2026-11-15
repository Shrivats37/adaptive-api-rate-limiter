// Plain-C++ test runner (no framework). Each CHECK prints PASS/FAIL; exit code is non-zero on any failure.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "ClientManager.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (cond) { ++g_pass; std::printf("  PASS  %s\n", msg); }                \
        else      { ++g_fail; std::printf("  FAIL  %s   (line %d)\n", msg, __LINE__); } \
    } while (0)

using ms = std::chrono::duration<double, std::milli>;
static Clock::time_point at(Clock::time_point base, double t_ms) {
    return base + std::chrono::duration_cast<Clock::duration>(ms(t_ms));
}

// ---- 1. ring buffer -------------------------------------------------------------------
static void test_ring_buffer() {
    std::puts("[ring buffer]");
    FeatureExtractor a;
    for (int i = 1; i <= 5; ++i) a.add_sample(i);
    CHECK(std::fabs(a.get_time_variance() - 2.0) < 1e-9, "variance of 1..5 is 2");

    FeatureExtractor b;
    for (int i = 1; i <= 25; ++i) b.add_sample(i);   // wraps: only 6..25 remain
    CHECK(std::fabs(b.get_time_variance() - 33.25) < 1e-9, "after wrap-around only the latest 20 samples count");

    FeatureExtractor c;
    CHECK(c.get_time_variance() == 100.0, "new client gets high default variance");
    CHECK(!c.ready(), "new client is not ready to be scored");
}

// ---- 2. token bucket maths with a fake clock -------------------------------------------
static void test_bucket_math() {
    std::puts("[token bucket, fake clock]");
    const auto t0 = Clock::now();
    TokenBucket b(5.0, 1.0, t0);
    int ok = 0;
    for (int i = 0; i < 8; ++i) ok += b.consume_at(t0);
    CHECK(ok == 5, "burst of 8 at once: exactly capacity (5) allowed");
    ok = 0;
    for (int i = 0; i < 5; ++i) ok += b.consume_at(at(t0, 2000));
    CHECK(ok == 2, "after 2 s with refill 1/s: exactly 2 more allowed");
    ok = 0;
    for (int i = 0; i < 3; ++i) ok += b.consume_at(at(t0, 1000));   // timestamp older than the last refill (2000 ms)
    CHECK(ok == 0, "out-of-order (older) timestamp does not create tokens");
    CHECK(b.allowed() == 7 && b.blocked() == 9, "atomic counters match (7 allowed, 9 blocked)");

    TokenBucket c(100.0, 10.0, t0);
    c.adapt_parameters(0.9, t0);
    ok = 0;
    for (int i = 0; i < 20; ++i) ok += c.consume_at(t0);
    CHECK(ok == 5, "high risk squeezes capacity to 5 immediately");
}

// ---- 3. concurrency on one bucket -------------------------------------------------------
static void test_threads_one_bucket() {
    std::puts("[8 threads on one bucket]");
    TokenBucket bucket(100.0, 10.0);
    const int THREADS = 8, PER = 100000;
    const auto t0 = Clock::now();
    std::vector<std::thread> pool;
    for (int t = 0; t < THREADS; ++t)
        pool.emplace_back([&] { for (int i = 0; i < PER; ++i) bucket.consume(1.0); });
    for (auto& th : pool) th.join();
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    CHECK(bucket.allowed() + bucket.blocked() == static_cast<std::uint64_t>(THREADS) * PER,
          "allowed + blocked equals total calls (no lost counts)");
    CHECK(bucket.allowed() <= static_cast<std::uint64_t>(100 + 10 * secs + 2),
          "never allowed more than capacity + refill over the elapsed time");
}

// ---- 4. client manager ------------------------------------------------------------------
static void test_client_manager() {
    std::puts("[client manager]");
    ClientManager mgr;
    const auto t0 = Clock::now();
    auto a = mgr.get_or_create("alice", t0);
    auto b = mgr.get_or_create("alice", t0);
    CHECK(a.get() == b.get(), "same id returns the same client object");
    CHECK(mgr.get_or_create("bob", t0).get() != a.get(), "different ids get different clients");

    ClientManager m2;
    std::vector<std::thread> pool;
    for (int t = 0; t < 8; ++t)
        pool.emplace_back([&] { for (int i = 0; i < 5000; ++i) m2.get_or_create("c" + std::to_string(i), Clock::now()); });
    for (auto& th : pool) th.join();
    CHECK(m2.size() == 5000, "8 threads creating overlapping ids give exactly 5000 clients");

    ClientManager m3;
    m3.check("old", t0, 100, 0);
    m3.check("fresh", at(t0, 599000), 100, 0);
    const std::size_t removed = m3.evict_idle(at(t0, 600000), std::chrono::seconds(300));
    CHECK(removed == 1 && m3.size() == 1, "idle client evicted after TTL, active one kept");
}

// ---- 5. behaviour detection (sanity check, NOT an evaluation) ----------------------------
static void test_detection() {
    std::puts("[detection sanity check on scripted traffic]");
    std::mt19937 g(1);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(g); };
    const auto t0 = Clock::now();

    ClientManager mgr;
    double t = 0;
    int bot_blocked_after_warmup = 0, bot_after = 0;
    bool cold_start_ok = true;
    double last_risk = 0;
    for (int i = 0; i < 100; ++i) {   // bot: ~8 req/s, near-fixed delay, same payload, same endpoint
        t += U(122, 128);
        Decision d = mgr.check("bot", at(t0, t), 512, 0);
        if (i < 5 && !d.allowed) cold_start_ok = false;
        if (i >= 20) { ++bot_after; bot_blocked_after_warmup += !d.allowed; }
        last_risk = d.risk;
    }
    CHECK(cold_start_ok, "first 5 requests of a new client are never blocked (cold start)");
    CHECK(last_risk > 0.7, "bot-like client ends in the high-risk band");
    CHECK(bot_blocked_after_warmup > 0.8 * bot_after, "more than 80% of bot requests blocked after warm-up");

    int human_blocked = 0;
    double max_risk = 0;
    t = 0;
    for (int i = 0; i < 100; ++i) {   // human: irregular gaps, varied payloads, many endpoints
        t += U(150, 1500);
        Decision d = mgr.check("human", at(t0, t), U(200, 2500), static_cast<unsigned>(U(0, 8)));
        human_blocked += !d.allowed;
        max_risk = std::max(max_risk, d.risk);
    }
    CHECK(human_blocked == 0, "human-like client is never blocked");
    CHECK(max_risk < 0.4, "human-like client stays in the low-risk band");
}

int main() {
    test_ring_buffer();
    test_bucket_math();
    test_threads_one_bucket();
    test_client_manager();
    test_detection();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
