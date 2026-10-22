#include <iostream>
#include <chrono>
#include "TokenBucket.h"
#include "FeatureExtractor.h"
#include "RiskEngine.h"

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << " Adaptive AI Rate Limiter - C++ Performance Suite  " << std::endl;
    std::cout << "====================================================" << std::endl;

    TokenBucket bucket(1000.0, 100.0);
    FeatureExtractor extractor;
    RiskEngine risk_engine;

    int total_requests = 100000;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < total_requests; ++i) {
        extractor.add_sample(12.5); // simulated inter-request time delta
        double risk_score = risk_engine.evaluate(4.2, 0.15, 0.8);
        bucket.adapt_parameters(risk_score);
        bucket.consume(1.0);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    double total_ms = elapsed.count();
    double avg_latency_us = (total_ms / total_requests) * 1000.0;
    double throughput = total_requests / (total_ms / 1000.0);

    std::cout << "Total Requests Processed : " << total_requests << std::endl;
    std::cout << "Total Execution Time     : " << total_ms << " ms" << std::endl;
    std::cout << "Average Latency per Req  : " << avg_latency_us << " microseconds" << std::endl;
    std::cout << "System Throughput        : " << throughput << " req/sec" << std::endl;
    std::cout << "====================================================" << std::endl;

    return 0;
}
