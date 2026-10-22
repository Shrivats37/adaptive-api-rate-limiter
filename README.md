# Adaptive AI-Powered API Rate Limiter & Threat Blocker

An ultra-low-latency, thread-safe API gateway middleware built completely from scratch in **Modern C++ (C++17)**. This project solves the critical limitations of traditional static rate limiters (such as Fixed Window or Sliding Window Counters) by detecting and mitigating low-and-slow automated bot attacks in real-time using behavioral feature extraction and lightweight logistic risk scoring.

---

## ⚡ Performance Benchmarks
Benchmarked locally on single-core CPU hardware over 100,000 synthetic request iterations:

| Metric | Measured Value |
| :--- | :--- |
| **Total Requests Evaluated** | 100,000 |
| **Average Latency per Request** | **0.83 µs** (sub-microsecond) |
| **Peak Throughput** | **~1.19 Million req/sec** |
| **Memory Footprint per Client** | $O(1)$ Fixed-Size Ring Buffer |

---

## 🛠️ System Architecture & Workflow

1. **Request Interception:** Captures incoming client metadata (IP/API Key).
2. **Sliding Window Feature Extractor:** Maintains a pre-allocated fixed-size ring buffer of recent inter-request time deltas to calculate statistical variance. (Bots exhibit near-zero variance due to automated script loops; humans have erratic timing).
3. **Lightweight Risk Scoring Engine:** Evaluates client behavior probability via a deterministic sigmoid dot product ($\sigma(W \cdot X + b)$) in nanoseconds without remote network or Python runtime overhead.
4. **Adaptive Token Bucket:** Dynamically squeezes or expands bucket capacity and refill rates based on the real-time threat risk score.

---

## 📂 Project Directory Structure

```text
api-rate-limiter/
├── CMakeLists.txt          # CMake build configuration
├── .gitignore              # Build artifacts exclusion rules
├── README.md               # Project documentation
├── include/
│   ├── TokenBucket.h       # Thread-safe adaptive token bucket implementation
│   ├── FeatureExtractor.h  # Ring buffer & sliding-window variance calculator
│   └── RiskEngine.h        # C++ logistic regression sigmoid classifier
└── src/
    └── main.cpp            # High-performance benchmark & simulation suite
