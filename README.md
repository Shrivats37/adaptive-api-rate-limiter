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
   -> ClientManager       sharded map (256 shards, one mutex each) finds the client's state
   -> FeatureExtractor    ring buffer of the last 20 requests -> 3 features
   -> RiskEngine          sigmoid(w . x + b), weights trained offline in Python
   -> smoothing           exponential moving average of the score (avoids flapping)
   -> TokenBucket         risk > 0.7: capacity 5, 0.5 tokens/s
                          risk > 0.4: capacity 20, 2 tokens/s
                          otherwise : capacity 100, 10 tokens/s  -> allow or 429
```

Features:

* `gap_cv`: standard deviation / mean of the time gaps between requests (low = machine-like)
* `payload_cv`: standard deviation / mean of payload sizes (low = machine-like)
* `top_endpoint_share`: share of requests that hit the most used endpoint (high = bot-like)

A client is not scored until 5 gaps are recorded (cold start), so new users are never blocked
on their first requests.

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

* **Token bucket**: O(1) state per client (tokens + last refill time), allows bursts, enforces a long-term rate. Refill is lazy (computed on arrival), so there is no background thread.
* **Concurrency**: one mutex per bucket protects the token arithmetic. Allowed/blocked counters are `std::atomic` and updated outside the lock, so monitoring never waits on the bucket. The client map is sharded, so requests for different clients rarely contend.
* **Ring buffer**: fixed `std::array` plus a wrapping head index; insertion is O(1) and memory per client is constant.
* **No heap allocation on the scoring path**: features and weights live on the stack or are `constexpr`.
* **Client lifetime**: `shared_ptr` entries plus TTL eviction of idle clients, so an in-flight request keeps its client alive even if the eviction thread removes it from the map.
* **Virtual clock**: the bucket and decision functions accept a timestamp, so tests and benchmarks are deterministic and fast.
* **Smoothing**: the risk score is an exponential moving average, so one odd request does not flip a client between buckets.

## Results

### Behaviour (demo, 10 s real time, one run on a MacBook Air)

| Client | Requests | Static bucket allowed | Adaptive allowed |
| --- | --- | --- | --- |
| Human (irregular timing, varied payload) | 11 | 11 (100%) | 11 (100%) |
| Low-and-slow bot (8 req/s, fixed payload) | 78 | 78 (100%) | 14 (18%) |
| Legitimate poller (cron job, 2 req/s) | 20 | 20 (100%) | 13 (65%) |

The bot stays under the static limit (8 req/s against a 10 req/s refill), so a static bucket
never stops it. The adaptive limiter first blocked it after about 1.3 s. The poller is a
legitimate client that behaves like a bot; it is wrongly throttled (see limitations).

### Model (synthetic data)

The model is trained on **synthetic traffic** generated in `tools/train_model.py`. No real
traffic was used, so these numbers show that the model separates the assumed behaviours, not
how it would perform in production.

| Held-out synthetic set (20,000 windows) | Result |
| --- | --- |
| ROC AUC | 0.999 |
| Threshold 0.7: precision / recall / false-positive rate | 0.987 / 0.967 / 0.007 |
| Naive bots flagged | 100% |
| Jittery bots (random delay) flagged | 92% |
| Hasty humans wrongly flagged | 3% |
| Legitimate regular pollers (cron jobs) flagged | 100% |

### Performance (`./dev.sh bench`)

The benchmark measures the full decision path (feature update, risk score, bucket) including
first-time creation of client state and map lookups, on a virtual clock with 20,000 clients
(20% bots) and 2,000,000 decisions. It does **not** include network or HTTP cost.
Three runs on a MacBook Air with 8 hardware threads:

| Threads | 16 shards | 256 shards |
| --- | --- | --- |
| 1 | 188-206 ns/decision | 197-219 ns/decision |
| 4 | 75-150 ns (noisy) | 54-68 ns |
| 8 | 125-148 ns | 54 ns (about 18.5M decisions/s) |
| 4 threads, one hot client | 290-298 ns | 295-308 ns |

With 16 shards, eight threads collided often on the shard locks. Raising the shard count to 256
cut the eight-thread cost from roughly 125-148 ns to a steady 54 ns, while the single-thread
and single-hot-client results did not change. That matches the lock-contention explanation; it
was not confirmed with a profiler. Throughput does not improve further from four to eight
threads, and I have not investigated why. One hot client stays at about 300 ns because all
threads wait on the same bucket lock, which is expected.

## Testing

`./dev.sh test` runs 20 checks:

* ring buffer: variance on a known sequence, wrap-around keeps exactly the latest 20 samples, cold-start defaults
* token bucket with a fake clock: exact burst size, exact refill after 2 s, older timestamps create no tokens, atomic counters, high risk squeezes capacity
* 8 threads on one bucket: no lost counts, never more allowed than capacity plus refill
* client map: same id gives the same object, 8 threads creating overlapping ids give an exact client count, idle eviction
* detection on scripted traffic: cold start never blocks, bot-like traffic ends in the high-risk band and is mostly blocked, human-like traffic is never blocked (a sanity check, not an evaluation)

## Known limitations

* **Single node.** State is in process memory. Several servers would each hold their own buckets; the standard fix is a shared store such as Redis with an atomic Lua script, or sticky routing by client id.
* **Weights are compiled in.** Retraining means regenerating `ModelWeights.h` and rebuilding.
* **Synthetic training data.** Real deployment needs labelled traffic and drift monitoring.
* **Regular but legitimate clients are flagged** (cron jobs, health checks, mobile apps polling). Mitigations: allowlist by API key, shadow mode (log the score without blocking), more features.
* **Evasion.** The model relies heavily on payload regularity. A bot that randomises timing, payload size and endpoints looks human to it. Timing regularity is one signal, not a complete bot detector.
* **Client identity.** Keying by an id the caller controls is weak against proxy rotation or shared IPs; production would combine API key, session and IP.
* **The adaptive layer only tightens limits for suspicious clients.** Normal clients get the same bucket a static limiter would give them, so this project does not claim fewer false positives than a static limiter.
* **Scaling flattens beyond four threads** in the benchmark; the reason is not investigated.

## Layout

```
include/   TokenBucket.h  RingStats.h  FeatureExtractor.h  RiskEngine.h  ClientManager.h  ModelWeights.h (generated)
src/       demo.cpp  bench.cpp  server.cpp
tests/     test_all.cpp
tools/     train_model.py
third_party/httplib.h   (cpp-httplib 0.18.0, MIT, single header)
docs/      model_report.txt
```