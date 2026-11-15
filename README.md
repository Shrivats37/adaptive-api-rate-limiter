# Adaptive API Rate Limiter & Threat Blocker (C++17)

A rate limiter that looks at **how a client behaves**, not only how many requests it sends.
Each client has a token bucket. A small logistic-regression model scores the client's recent
behaviour (timing regularity, payload regularity, endpoint repetition) and the score
shrinks or restores that client's bucket.

## Why

A static limit such as "100 requests per minute" only counts volume. A bot that sends
8 requests per second with almost fixed timing stays under the limit and is never stopped.
This project adds a behavioural signal on top of the classic token bucket.

## How a request is decided

```
request (client id, payload size, endpoint)
   -> ClientManager       sharded map (16 shards, one mutex each) finds the client's state
   -> FeatureExtractor    ring buffer of the last 20 requests -> 3 features
   -> RiskEngine          sigmoid(w . x + b), weights trained offline in Python
   -> smoothing           exponential moving average of the score (avoids flapping)
   -> TokenBucket         risk > 0.7: capacity 5, 0.5 tokens/s
                          risk > 0.4: capacity 20, 2 tokens/s
                          otherwise : capacity 100, 10 tokens/s  -> allow or 429
```

Features: `gap_cv` (std/mean of time gaps), `payload_cv` (std/mean of payload sizes),
`top_endpoint_share` (share of the most used endpoint). A client is not scored until it has
5 gaps recorded (cold start), so new users are never blocked on their first requests.

## Run it

```bash
./dev.sh test      # 20 checks: ring buffer, bucket maths, 8-thread stress, client map, detection
./dev.sh demo      # 10 s live demo: static limiter vs adaptive limiter
./dev.sh bench     # benchmark with virtual clock, 20,000 clients, 2,000,000 decisions
./dev.sh server    # REST service on 127.0.0.1:8080
./dev.sh train     # regenerate include/ModelWeights.h from synthetic data (needs numpy, scikit-learn)
```

REST service:

```bash
curl "http://127.0.0.1:8080/check?client=bot1&payload=512&endpoint=0"
# 200 {"allowed":true,"risk":0.000}     or     429 + Retry-After: 2
curl http://127.0.0.1:8080/stats
```

## Design decisions

* **Token bucket**: O(1) state per client (tokens + last refill time), allows bursts, enforces a long-term rate. Refill is lazy (computed on arrival), so no background thread.
* **Concurrency**: one mutex per bucket protects the token arithmetic. Allowed/blocked counters are `std::atomic` and updated outside the lock, so monitoring never waits on the bucket. Client map is sharded, so different clients rarely contend.
* **Ring buffer**: fixed `std::array` + wrapping head index; insertion is O(1) and memory per client is constant.
* **No heap allocation on the scoring path**: features and weights live on the stack / are `constexpr`.
* **Client lifetime**: `shared_ptr` entries plus TTL eviction of idle clients; an in-flight request keeps its client alive.
* **Virtual clock**: bucket and decision functions accept a timestamp, so tests and benchmarks are deterministic and fast.

## Evaluation (read this honestly)

The model is trained on **synthetic traffic** generated in `tools/train_model.py`. No real
traffic was used, so the numbers below show that the model separates the assumed behaviours,
not how it would perform in production.

| Held-out synthetic set (20,000 windows) | Result |
| --- | --- |
| ROC AUC | 0.999 |
| Threshold 0.7: precision / recall / false-positive rate | 0.987 / 0.967 / 0.007 |
| Naive bots flagged | 100% |
| Jittery bots (random delay) flagged | 92% |
| Hasty humans wrongly flagged | 3% |
| Legitimate regular pollers (cron jobs) flagged | 100% |

Demo result (10 s, real time, one run): a bot sending 8 req/s was allowed 100% by a static
bucket (100 capacity, 10/s) and 18% by the adaptive limiter; it was first blocked after about
1.2 s. A normal user was never blocked.

Benchmark (`./dev.sh bench`): measures the full decision path including client creation and
map lookups on a virtual clock. It does not include network or HTTP cost. Run it on your own
machine and record the numbers you see; they depend on hardware and thread count.

## Known limitations

* **Single node.** State is in process memory. Several servers would each hold their own buckets; the standard fix is a shared store such as Redis with an atomic Lua script, or sticky routing by client id.
* **Weights are compiled in.** Retraining means regenerating `ModelWeights.h` and rebuilding.
* **Synthetic training data.** Real deployment needs labelled traffic and drift monitoring.
* **Regular but legitimate clients are flagged** (cron jobs, health checks, mobile apps polling). Mitigations: allowlist by API key, shadow mode (log the score without blocking), more features.
* **Evasion.** A bot that randomises timing *and* payload size *and* spreads across endpoints looks human to this model. Timing regularity is one signal, not a complete bot detector.
* **Client identity.** Keying by a client id the caller controls is weak against proxy rotation or shared IPs; production would combine API key, session and IP.
* **The adaptive layer only tightens limits for suspicious clients.** Normal clients get the same bucket a static limiter would give them, so this project does not claim fewer false positives than a static limiter.

## Layout

```
include/   TokenBucket.h  RingStats.h  FeatureExtractor.h  RiskEngine.h  ClientManager.h  ModelWeights.h (generated)
src/       demo.cpp  bench.cpp  server.cpp
tests/     test_all.cpp
tools/     train_model.py
third_party/httplib.h   (cpp-httplib 0.18.0, MIT, single header)
docs/      model_report.txt
```
