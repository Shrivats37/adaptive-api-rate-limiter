#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "FeatureExtractor.h"
#include "RiskEngine.h"
#include "TokenBucket.h"

struct Decision {
    bool allowed;
    double risk;   // smoothed risk score used for this decision
};

// All state of one client: behaviour window, smoothed risk, and its token bucket.
class ClientState {
private:
    static constexpr double EMA_ALPHA = 0.3;   // weight of the newest score

    std::mutex m_;                 // protects fx_, risk_ema_, last_seen_
    FeatureExtractor fx_;
    RiskEngine engine_;
    double risk_ema_ = 0.0;
    bool scored_ = false;
    bool has_last_ = false;
    Clock::time_point last_seen_;
    TokenBucket bucket_;

public:
    explicit ClientState(Clock::time_point now) : last_seen_(now), bucket_(100.0, 10.0, now) {}

    Decision decide(Clock::time_point now, double payload_bytes, unsigned endpoint) {
        double risk;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (has_last_) {
                double gap_ms = std::chrono::duration<double, std::milli>(now - last_seen_).count();
                if (gap_ms < 0.0) gap_ms = 0.0;
                fx_.add_request(gap_ms, payload_bytes, endpoint);
            }
            has_last_ = true;
            last_seen_ = now;

            if (fx_.ready()) {   // cold start: until enough samples, the client is treated as human
                const double r = engine_.evaluate(fx_.features());
                risk_ema_ = scored_ ? EMA_ALPHA * r + (1.0 - EMA_ALPHA) * risk_ema_ : r;
                scored_ = true;
            }
            risk = risk_ema_;
        }   // m_ released before touching the bucket, so we never hold two locks at once

        bucket_.adapt_parameters(risk, now);
        return Decision{bucket_.consume_at(now), risk};
    }

    bool idle_longer_than(Clock::time_point now, std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(m_);
        return now - last_seen_ > ttl;
    }

    const TokenBucket& bucket() const { return bucket_; }
};

// Sharded client map: N partitions, each with its own mutex.
// A client id is hashed to pick the partition, so requests for different
// clients rarely wait for each other.
class ClientManager {
private:
    static constexpr std::size_t NUM_SHARDS = 16;

    struct Shard {
        mutable std::mutex mtx;
        std::unordered_map<std::string, std::shared_ptr<ClientState>> clients;
    };
    std::array<Shard, NUM_SHARDS> shards_;

    Shard& shard_for(const std::string& id) {
        return shards_[std::hash<std::string>{}(id) % NUM_SHARDS];
    }

public:
    // shared_ptr (not unique_ptr) so a thread that is still using a client keeps it
    // alive even if the eviction thread removes it from the map.
    std::shared_ptr<ClientState> get_or_create(const std::string& id, Clock::time_point now) {
        Shard& s = shard_for(id);
        std::lock_guard<std::mutex> lock(s.mtx);
        auto it = s.clients.find(id);
        if (it == s.clients.end()) {
            it = s.clients.emplace(id, std::make_shared<ClientState>(now)).first;
        }
        return it->second;
    }

    Decision check(const std::string& id, Clock::time_point now, double payload_bytes, unsigned endpoint) {
        return get_or_create(id, now)->decide(now, payload_bytes, endpoint);
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const Shard& s : shards_) {
            std::lock_guard<std::mutex> lock(s.mtx);
            total += s.clients.size();
        }
        return total;
    }

    // Remove clients that have been idle for longer than ttl. Returns how many were removed.
    // Lock order is always shard -> client, and decide() never takes a shard lock while
    // holding a client lock, so this cannot deadlock.
    std::size_t evict_idle(Clock::time_point now, std::chrono::seconds ttl) {
        std::size_t removed = 0;
        for (Shard& s : shards_) {
            std::lock_guard<std::mutex> lock(s.mtx);
            for (auto it = s.clients.begin(); it != s.clients.end();) {
                if (it->second->idle_longer_than(now, ttl)) {
                    it = s.clients.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
        }
        return removed;
    }
};
