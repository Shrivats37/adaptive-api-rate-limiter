#include <iostream>
#include <thread>
#include <vector>
#include "TokenBucket.h"

int main() {
    TokenBucket bucket(100.0, 10.0);
    const int THREADS = 8, PER_THREAD = 100000;

    std::vector<std::thread> pool;
    for (int t = 0; t < THREADS; ++t)
        pool.emplace_back([&] {
            for (int i = 0; i < PER_THREAD; ++i) bucket.consume(1.0);
        });
    for (auto& th : pool) th.join();

    std::uint64_t total = bucket.allowed() + bucket.blocked();
    std::cout << "allowed=" << bucket.allowed()
              << " blocked=" << bucket.blocked()
              << " total=" << total
              << " (expect " << THREADS * PER_THREAD << ")\n";
}