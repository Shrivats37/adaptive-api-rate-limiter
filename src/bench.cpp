// Benchmark of the full decision path (feature update + risk score + bucket) using a
// VIRTUAL clock, so the traffic is realistic and the run is fast and repeatable.
//
// What it measures : decisions per second of ClientManager::check(), including first-touch
//                    creation of client state, on a mixed population of humans and bots.
// What it does NOT : network I/O, HTTP parsing, or real production traffic.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "ClientManager.h"

namespace {
constexpr std::size_t N_CLIENTS = 20000;
constexpr std::size_t N_EVENTS = 2000000;
constexpr double BOT_FRACTION = 0.20;

struct Event {
    std::uint32_t client;
    double t_ms;
    std::uint16_t payload;
    std::uint8_t endpoint;
};

struct ClientSpec { bool bot; double base_gap_ms; double base_payload; };

std::vector<Event> generate(std::vector<ClientSpec>& specs, std::mt19937& rng) {
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::lognormal_distribution<double> LN(0.0, 1.0);
    specs.resize(N_CLIENTS);
    using Item = std::pair<double, std::uint32_t>;   // (next time, client)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (std::uint32_t c = 0; c < N_CLIENTS; ++c) {
        ClientSpec& s = specs[c];
        s.bot = U(rng) < BOT_FRACTION;
        s.base_gap_ms = s.bot ? 125.0 : 1000.0;
        s.base_payload = 200.0 + 2000.0 * U(rng);
        pq.push({U(rng) * 1000.0, c});
    }
    std::vector<Event> ev;
    ev.reserve(N_EVENTS);
    while (ev.size() < N_EVENTS) {
        auto [t, c] = pq.top(); pq.pop();
        const ClientSpec& s = specs[c];
        Event e{c, t, 0, 0};
        double gap;
        if (s.bot) {   // nearly fixed delay, same payload, same endpoint
            gap = s.base_gap_ms * (0.97 + 0.06 * U(rng));
            e.payload = static_cast<std::uint16_t>(s.base_payload);
            e.endpoint = 0;
        } else {       // irregular delay, varied payload, many endpoints
            gap = s.base_gap_ms * LN(rng);
            e.payload = static_cast<std::uint16_t>(200 + 2300 * U(rng));
            e.endpoint = static_cast<std::uint8_t>(U(rng) * 8);
        }
        ev.push_back(e);
        pq.push({t + std::max(gap, 1.0), c});
    }
    return ev;
}

struct Result {
    double seconds = 0;
    std::uint64_t bot_total = 0, bot_blocked = 0, human_total = 0, human_blocked = 0;
};

Result run(const std::vector<Event>& ev, const std::vector<ClientSpec>& specs,
           const std::vector<std::string>& ids, unsigned threads) {
    ClientManager mgr;
    const auto base = Clock::now();
    std::vector<Result> parts(threads);
    // Partition outside the timed region: one client is always handled by one thread, in time order.
    std::vector<std::vector<std::uint32_t>> mine(threads);
    for (std::uint32_t i = 0; i < ev.size(); ++i) mine[ev[i].client % threads].push_back(i);
    auto work = [&](unsigned k) {
        Result& r = parts[k];
        for (std::uint32_t idx : mine[k]) {
            const Event& e = ev[idx];
            const auto now = base + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double, std::milli>(e.t_ms));
            const Decision d = mgr.check(ids[e.client], now, e.payload, e.endpoint);
            if (specs[e.client].bot) { ++r.bot_total; r.bot_blocked += !d.allowed; }
            else { ++r.human_total; r.human_blocked += !d.allowed; }
        }
    };
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < threads; ++k) pool.emplace_back(work, k);
    for (auto& th : pool) th.join();
    const auto t1 = std::chrono::steady_clock::now();
    Result total;
    total.seconds = std::chrono::duration<double>(t1 - t0).count();
    for (const Result& r : parts) {
        total.bot_total += r.bot_total; total.bot_blocked += r.bot_blocked;
        total.human_total += r.human_total; total.human_blocked += r.human_blocked;
    }
    return total;
}
}  // namespace

int main() {
    std::mt19937 rng(12345);
    std::vector<ClientSpec> specs;
    std::printf("Generating %zu events for %zu clients (%.0f%% bots)...\n", N_EVENTS, N_CLIENTS, BOT_FRACTION * 100);
    const std::vector<Event> ev = generate(specs, rng);
    std::vector<std::string> ids(N_CLIENTS);
    for (std::size_t i = 0; i < N_CLIENTS; ++i) ids[i] = "client-" + std::to_string(i);

    std::printf("\nHardware threads on this machine: %u\n", std::thread::hardware_concurrency());
    std::printf("\n%-8s %12s %14s %12s   %s\n", "threads", "time (s)", "decisions/s", "ns/decision", "blocked: bots / humans");
    for (unsigned t : {1u, 2u, 4u, 8u}) {
        const Result r = run(ev, specs, ids, t);
        const double n = static_cast<double>(r.bot_total + r.human_total);
        std::printf("%-8u %12.3f %14.0f %12.0f   %5.1f%% / %4.1f%%\n", t, r.seconds, n / r.seconds,
                    r.seconds / n * 1e9, 100.0 * r.bot_blocked / std::max<std::uint64_t>(r.bot_total, 1),
                    100.0 * r.human_blocked / std::max<std::uint64_t>(r.human_total, 1));
    }

    // Worst case: every thread hammers ONE client (real clock), so they all fight for the same locks.
    {
        ClientManager mgr;
        const unsigned T = 4;
        const std::size_t PER = 250000;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned k = 0; k < T; ++k)
            pool.emplace_back([&] { for (std::size_t i = 0; i < PER; ++i) mgr.check("hot-client", Clock::now(), 512, 0); });
        for (auto& th : pool) th.join();
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\nHot-key test (%u threads, one client, real clock): %.0f decisions/s, %.0f ns/decision\n",
                    T, T * PER / s, s / (T * PER) * 1e9);
    }
    std::printf("\nNote: this is a micro-benchmark of the decision logic, not an end-to-end server benchmark.\n");
    return 0;
}
