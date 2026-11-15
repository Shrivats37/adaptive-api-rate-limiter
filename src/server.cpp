// Tiny REST service around the limiter, in the style of an "ask before you serve" sidecar.
//   GET /check?client=ID&payload=BYTES&endpoint=N  ->  200 {"allowed":true,...}  or  429 + Retry-After
//   GET /stats                                      ->  counters
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "ClientManager.h"
#include "httplib.h"

namespace {
double num(const httplib::Request& req, const char* key, double def) {
    if (!req.has_param(key)) return def;
    try { return std::stod(req.get_param_value(key)); } catch (...) { return def; }
}
}  // namespace

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    ClientManager mgr;
    std::atomic<std::uint64_t> allowed{0}, blocked{0};
    httplib::Server svr;

    svr.Get("/check", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("client") || req.get_param_value("client").empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"missing client parameter\"}", "application/json");
            return;
        }
        const std::string id = req.get_param_value("client");
        const double payload = num(req, "payload", 0);
        const unsigned endpoint = static_cast<unsigned>(num(req, "endpoint", 0));
        const Decision d = mgr.check(id, Clock::now(), payload, endpoint);
        char body[128];
        std::snprintf(body, sizeof body, "{\"allowed\":%s,\"risk\":%.3f}", d.allowed ? "true" : "false", d.risk);
        res.set_content(body, "application/json");
        if (d.allowed) {
            allowed.fetch_add(1, std::memory_order_relaxed);
        } else {
            blocked.fetch_add(1, std::memory_order_relaxed);
            res.status = 429;
            res.set_header("Retry-After", "2");
        }
    });

    svr.Get("/stats", [&](const httplib::Request&, httplib::Response& res) {
        char body[160];
        std::snprintf(body, sizeof body, "{\"clients\":%zu,\"allowed\":%llu,\"blocked\":%llu}",
                      mgr.size(), static_cast<unsigned long long>(allowed.load()),
                      static_cast<unsigned long long>(blocked.load()));
        res.set_content(body, "application/json");
    });

    std::printf("Rate limiter service on http://127.0.0.1:%d  (try /check?client=bot1&payload=512&endpoint=0)\n", port);
    svr.listen("127.0.0.1", port);   // bound to localhost only
    return 0;
}
