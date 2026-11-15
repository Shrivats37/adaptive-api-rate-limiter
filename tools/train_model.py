#!/usr/bin/env python3
"""
Train the logistic regression used by RiskEngine and export its weights as C++ constants.

IMPORTANT: there is no public labelled dataset for this problem, so the training data is
SYNTHETIC. It is generated from simple behavioural assumptions written in this file
(humans are irregular, bots are regular, some bots add random jitter, some humans are
hasty). Metrics printed here therefore describe how well the model separates these
assumed behaviours. They are not a claim about real-world traffic.

Features (computed exactly like FeatureExtractor in C++):
    gap_cv              std/mean of the time gaps in the window
    payload_cv          std/mean of the payload sizes in the window
    top_endpoint_share  fraction of requests that hit the most used endpoint

Usage:  python3 tools/train_model.py
Needs:  pip install numpy scikit-learn
"""
from pathlib import Path
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_auc_score

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "ModelWeights.h"
REPORT = ROOT / "docs" / "model_report.txt"

WINDOW_MAX, WINDOW_MIN = 20, 5


def cv(x):
    m = x.mean()
    return 0.0 if m < 1e-9 else float(x.std() / m)       # population std, same as C++


def top_share(eps):
    _, counts = np.unique(eps, return_counts=True)
    return float(counts.max() / len(eps))


def features(gaps, payloads, eps):
    return [cv(gaps), cv(payloads), top_share(eps)]


def pick_endpoints(rng, n, k, alpha):
    probs = rng.dirichlet(np.full(k, alpha))
    return rng.choice(k, size=n, p=probs)


# ---- traffic generators: each returns one window (gaps, payloads, endpoints) ----------
def human_normal(rng, n):
    gaps = rng.lognormal(np.log(rng.uniform(300, 3000)), rng.uniform(0.6, 1.3), n)
    pay = rng.lognormal(np.log(rng.uniform(300, 3000)), rng.uniform(0.3, 0.9), n)
    return gaps, pay, pick_endpoints(rng, n, int(rng.integers(4, 13)), 1.0)


def human_hasty(rng, n):          # flash-sale style refreshing: faster and more regular
    gaps = rng.lognormal(np.log(rng.uniform(50, 300)), rng.uniform(0.35, 0.7), n)
    pay = rng.lognormal(np.log(rng.uniform(300, 3000)), rng.uniform(0.1, 0.4), n)
    return gaps, pay, pick_endpoints(rng, n, int(rng.integers(2, 5)), 0.6)


def bot_naive(rng, n):            # fixed delay with tiny jitter, same payload, 1-2 endpoints
    base, j = rng.uniform(50, 2000), rng.uniform(0.01, 0.08)
    gaps = base * (1 + rng.uniform(-j, j, n))
    p = rng.uniform(100, 3000)
    pay = p * (1 + rng.uniform(-0.03, 0.03, n))
    return gaps, pay, pick_endpoints(rng, n, int(rng.integers(1, 3)), 0.3)


def bot_jittery(rng, n):          # random delay to look human, but payload/endpoints stay repetitive
    base = rng.uniform(50, 2000)
    gaps = base * rng.uniform(0.3, 1.7, n)
    p, pj = rng.uniform(100, 3000), rng.uniform(0.0, 0.15)
    pay = p * (1 + rng.uniform(-pj, pj, n))
    return gaps, pay, pick_endpoints(rng, n, int(rng.integers(1, 4)), 0.4)


def legit_poller(rng, n):         # e.g. a cron job or health check: regular on purpose, but NOT a bot
    base = rng.uniform(500, 5000)
    gaps = base * (1 + rng.uniform(-0.02, 0.02, n))
    pay = np.full(n, rng.uniform(100, 1000))
    return gaps, pay, np.zeros(n, dtype=int)


# type name, generator, label (1 = bot), weight in the training mix
TYPES = [
    ("human_normal", human_normal, 0, 0.50),
    ("human_hasty", human_hasty, 0, 0.15),
    ("bot_naive", bot_naive, 1, 0.20),
    ("bot_jittery", bot_jittery, 1, 0.15),
]


def make_dataset(rng, size):
    names, gens, labels, probs = zip(*TYPES)
    X, y, kind = [], [], []
    for _ in range(size):
        i = rng.choice(len(TYPES), p=probs)
        n = int(rng.integers(WINDOW_MIN, WINDOW_MAX + 1))
        g, p, e = gens[i](rng, n)
        X.append(features(g, p, e))
        y.append(labels[i])
        kind.append(names[i])
    return np.array(X), np.array(y), np.array(kind)


def main():
    rng = np.random.default_rng(42)
    Xtr, ytr, _ = make_dataset(rng, 20000)
    Xte, yte, kte = make_dataset(np.random.default_rng(7), 20000)

    model = LogisticRegression(C=1.0, max_iter=1000).fit(Xtr, ytr)
    w, b = model.coef_[0], float(model.intercept_[0])
    prob = model.predict_proba(Xte)[:, 1]

    lines = []
    out = lines.append
    out("Synthetic-data evaluation (held-out set of 20000 windows, different seed)")
    out("Weights: gap_cv=%.4f payload_cv=%.4f top_endpoint_share=%.4f bias=%.4f" % (*w, b))
    out("ROC AUC: %.4f" % roc_auc_score(yte, prob))
    for thr in (0.5, 0.7):
        pred = prob > thr
        tp = int(((pred == 1) & (yte == 1)).sum()); fp = int(((pred == 1) & (yte == 0)).sum())
        fn = int(((pred == 0) & (yte == 1)).sum()); tn = int(((pred == 0) & (yte == 0)).sum())
        out("threshold %.1f  precision=%.3f recall=%.3f false-positive-rate=%.3f"
            % (thr, tp / max(tp + fp, 1), tp / max(tp + fn, 1), fp / max(fp + tn, 1)))
    out("Per type, share flagged as high risk (> 0.7):")
    for name, _, label, _ in TYPES:
        m = kte == name
        out("  %-13s %s  flagged = %.3f" % (name, "(bot)  " if label else "(human)", float((prob[m] > 0.7).mean())))

    prng = np.random.default_rng(99)
    Xp = np.array([features(*legit_poller(prng, int(prng.integers(WINDOW_MIN, WINDOW_MAX + 1)))) for _ in range(2000)])
    pp = model.predict_proba(Xp)[:, 1]
    out("  %-13s (human)   flagged = %.3f   <-- known weakness: regular but legitimate clients" % ("legit_poller", float((pp > 0.7).mean())))

    text = "\n".join(lines)
    print(text)
    REPORT.parent.mkdir(exist_ok=True)
    REPORT.write_text(text + "\n")

    HEADER.write_text(
        "// Generated by tools/train_model.py. Do not edit by hand.\n"
        "// Trained on SYNTHETIC traffic; see docs/model_report.txt.\n"
        "#pragma once\n#include <array>\n\nnamespace model {\n"
        "inline constexpr std::array<double, 3> WEIGHTS = {%.6f, %.6f, %.6f};  // gap_cv, payload_cv, top_endpoint_share\n"
        "inline constexpr double BIAS = %.6f;\n}  // namespace model\n" % (*w, b))
    print("\nWrote", HEADER)


if __name__ == "__main__":
    main()
