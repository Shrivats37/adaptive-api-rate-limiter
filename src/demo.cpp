// Live demo: three simulated clients hit the SAME traffic through
//   (a) a static token bucket (100 capacity, 10 tokens/s) and
//   (b) the adaptive limiter (behaviour score -> bucket size).
// Runs in real time for DURATION_S seconds, one thread per client.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "ClientManager.h"

namespace {
constexpr int DURATION_S = 10;

struct Tally {
    std::atomic<int> total{0}, adaptive_allowed{0}, static_allowed{0};
    std::atomic<int> first_adaptive_block{-1};
};

using GapFn = double (*)(std::mt19937&);
struct Profile {
    const char* label;
    const char* id;
    double (*gap_ms)(std::mt19937&);
    double (*payload)(std::mt19937&);
    unsigned (*endpoint)(std::mt19937&);
};

double uni(std::mt19937& g, double a, double b) { return std::uniform_real_distribution<double>(a, b)(g); }

// Human: irregular gaps, varied payloads, several endpoints.
double human_gap(std::mt19937& g) { return uni(g, 150, 1500); }
double human_pay(std::mt19937& g) { return uni(g, 200, 2500); }
unsigned human_ep(std::mt19937& g) { return std::uniform_int_distribution<unsigned>(0, 7)(g); }
// Low-and-slow bot: 8 requests/s (under the static limit), almost fixed delay, same payload, same endpoint.
double bot_gap(std::mt19937& g) { return uni(g, 122, 128); }
double bot_pay(std::mt19937&) { return 512; }
unsigned bot_ep(std::mt19937&) { return 0; }
// Legitimate poller (cron job): regular on purpose.
double poll_gap(std::mt19937&) { return 500; }
double poll_pay(std::mt19937&) { return 256; }
unsigned poll_ep(std::mt19937&) { return 3; }

void run_client(const Profile& p, ClientManager& mgr, Tally& t, Clock::time_point end) {
    std::mt19937 gen(std::hash<std::string>{}(p.id));
    TokenBucket static_bucket(100.0, 10.0);
    const auto start = Clock::now();
    while (Clock::now() < end) {
        const auto now = Clock::now();
        const Decision d = mgr.check(p.id, now, p.payload(gen), p.endpoint(gen));
        const bool s = static_bucket.consume();
        ++t.total;
        if (d.allowed) ++t.adaptive_allowed;
        else if (t.first_adaptive_block < 0)
            t.first_adaptive_block = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (s) ++t.static_allowed;
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(p.gap_ms(gen)));
    }
}
}  // namespace

int main() {
    const Profile profiles[3] = {
        {"human", "user-alice", human_gap, human_pay, human_ep},
        {"low-and-slow bot", "bot-7", bot_gap, bot_pay, bot_ep},
        {"legit poller (false-positive example)", "cron-job", poll_gap, poll_pay, poll_ep},
    };
    ClientManager mgr;
    Tally tallies[3];
    std::printf("Running %d s of simulated traffic in real time...\n", DURATION_S);
    const auto end = Clock::now() + std::chrono::seconds(DURATION_S);
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i)
        threads.emplace_back(run_client, std::cref(profiles[i]), std::ref(mgr), std::ref(tallies[i]), end);
    for (auto& th : threads) th.join();

    std::printf("\n%-40s %8s %18s %18s\n", "client", "requests", "static: allowed", "adaptive: allowed");
    for (int i = 0; i < 3; ++i) {
        const int n = tallies[i].total;
        std::printf("%-40s %8d %11d (%3.0f%%) %12d (%3.0f%%)\n", profiles[i].label, n,
                    tallies[i].static_allowed.load(), 100.0 * tallies[i].static_allowed / n,
                    tallies[i].adaptive_allowed.load(), 100.0 * tallies[i].adaptive_allowed / n);
    }
    const int fb = tallies[1].first_adaptive_block;
    if (fb >= 0) std::printf("\nBot was first blocked by the adaptive limiter after %d ms.\n", fb);
    std::printf("Static limiter allowed every bot request because 8 req/s is under its 10 req/s refill rate.\n");
    std::printf("The poller is a legitimate client that looks like a bot: a known weakness (see README).\n");
    return 0;
}
