// ─── NanoMatch Standalone Benchmark ──────────────────────────────────────────
//
// Runs BOTH the optimized engine and naive STL baseline side-by-side,
// prints a comparison table with throughput and latency percentiles.
//
// Build (Linux/GCC):
//   g++ -std=c++17 -O3 -Iinclude src/order_book.cpp bench/bench_standalone.cpp \
//       -o bench_standalone && ./bench_standalone data/orders_500k.csv
//
// Build (Windows/MSVC):
//   cl /std:c++17 /O2 /EHsc /Iinclude src\order_book.cpp bench\bench_standalone.cpp \
//       /Fe:bench_standalone.exe && bench_standalone.exe data\small.csv
//
// No external dependencies — pure C++17.
// ─────────────────────────────────────────────────────────────────────────────

#include "order_book.hpp"
#include "order_book_naive.hpp"
#include "csv_parser.hpp"
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <memory>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

#if defined(_MSC_VER)
#include <intrin.h>
static inline uint64_t rdtsc_now() { return __rdtsc(); }
#else
static inline uint64_t rdtsc_now() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

// ─── Timing helpers ──────────────────────────────────────────────────────────

struct TimingResult {
    double wall_ms;
    uint64_t cycles;
    uint64_t trades;
    uint64_t orders;
};

// ─── Percentile calculator ───────────────────────────────────────────────────

struct LatencyStats {
    double p50_ns;
    double p90_ns;
    double p99_ns;
    double p999_ns;
    double mean_ns;
};

static LatencyStats compute_percentiles(std::vector<double>& latencies) {
    if (latencies.empty()) return {0, 0, 0, 0, 0};
    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    return {
        latencies[static_cast<size_t>(n * 0.50)],
        latencies[static_cast<size_t>(n * 0.90)],
        latencies[std::min(static_cast<size_t>(n * 0.99), n - 1)],
        latencies[std::min(static_cast<size_t>(n * 0.999), n - 1)],
        sum / static_cast<double>(n)
    };
}

// ─── Calibrate ns per TSC tick ───────────────────────────────────────────────

static double calibrate_tsc_ns() {
    auto t0 = std::chrono::high_resolution_clock::now();
    uint64_t c0 = rdtsc_now();

    // Spin for ~50ms to get a stable calibration
    volatile uint64_t dummy = 0;
    for (int i = 0; i < 10'000'000; ++i) dummy += i;

    uint64_t c1 = rdtsc_now();
    auto t1 = std::chrono::high_resolution_clock::now();

    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double ticks = static_cast<double>(c1 - c0);
    return ns / ticks;  // nanoseconds per tick
}

// ─── Benchmark: Optimized Engine ─────────────────────────────────────────────

static TimingResult bench_optimized(const std::vector<OrderMsg>& orders, int runs) {
    double best_ms = 1e18;
    uint64_t best_cycles = UINT64_MAX;
    uint64_t trades = 0;

    for (int r = 0; r < runs; ++r) {
        auto book = std::make_unique<OrderBook>();
        book->on_trade = nullptr;  // no callback overhead

        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t c0 = rdtsc_now();

        replay(*book, orders);

        uint64_t c1 = rdtsc_now();
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        uint64_t cycles = c1 - c0;

        if (ms < best_ms) {
            best_ms = ms;
            best_cycles = cycles;
            trades = book->total_trades();
        }
    }

    return { best_ms, best_cycles, trades, orders.size() };
}

// ─── Benchmark: Naive Engine ─────────────────────────────────────────────────

static TimingResult bench_naive(const std::vector<OrderMsg>& orders, int runs) {
    double best_ms = 1e18;
    uint64_t best_cycles = UINT64_MAX;
    uint64_t trades = 0;

    for (int r = 0; r < runs; ++r) {
        auto book = std::make_unique<NaiveOrderBook>();
        book->on_trade = nullptr;

        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t c0 = rdtsc_now();

        replay(*book, orders);

        uint64_t c1 = rdtsc_now();
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        uint64_t cycles = c1 - c0;

        if (ms < best_ms) {
            best_ms = ms;
            best_cycles = cycles;
            trades = book->total_trades();
        }
    }

    return { best_ms, best_cycles, trades, orders.size() };
}

// ─── Latency benchmark: per-order timing ─────────────────────────────────────

static LatencyStats latency_optimized(const std::vector<OrderMsg>& orders,
                                       double ns_per_tick) {
    auto book = std::make_unique<OrderBook>();
    book->on_trade = nullptr;

    // Warm up with first half
    size_t warmup = orders.size() / 2;
    for (size_t i = 0; i < warmup; ++i) {
        const auto& m = orders[i];
        switch (m.type) {
            case OrderType::LIMIT:  book->add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book->add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book->cancel_order(m.price); break;
        }
    }

    // Measure second half
    size_t measure_count = orders.size() - warmup;
    std::vector<double> latencies;
    latencies.reserve(measure_count);

    for (size_t i = warmup; i < orders.size(); ++i) {
        const auto& m = orders[i];
        uint64_t c0 = rdtsc_now();
        switch (m.type) {
            case OrderType::LIMIT:  book->add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book->add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book->cancel_order(m.price); break;
        }
        uint64_t c1 = rdtsc_now();
        latencies.push_back(static_cast<double>(c1 - c0) * ns_per_tick);
    }

    return compute_percentiles(latencies);
}

static LatencyStats latency_naive(const std::vector<OrderMsg>& orders,
                                   double ns_per_tick) {
    auto book = std::make_unique<NaiveOrderBook>();
    book->on_trade = nullptr;

    size_t warmup = orders.size() / 2;
    for (size_t i = 0; i < warmup; ++i) {
        const auto& m = orders[i];
        switch (m.type) {
            case OrderType::LIMIT:  book->add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book->add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book->cancel_order(m.price); break;
        }
    }

    size_t measure_count = orders.size() - warmup;
    std::vector<double> latencies;
    latencies.reserve(measure_count);

    for (size_t i = warmup; i < orders.size(); ++i) {
        const auto& m = orders[i];
        uint64_t c0 = rdtsc_now();
        switch (m.type) {
            case OrderType::LIMIT:  book->add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book->add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book->cancel_order(m.price); break;
        }
        uint64_t c1 = rdtsc_now();
        latencies.push_back(static_cast<double>(c1 - c0) * ns_per_tick);
    }

    return compute_percentiles(latencies);
}

// ─── Match-only micro benchmark ──────────────────────────────────────────────
// Alternating buy/sell at same price — every order matches immediately.
// Isolates pure matching throughput from price lookup overhead.

static TimingResult bench_matching_only_optimized(int count, int runs) {
    double best_ms = 1e18;
    uint64_t best_cycles = UINT64_MAX;
    uint64_t trades = 0;

    for (int r = 0; r < runs; ++r) {
        auto book = std::make_unique<OrderBook>();
        book->on_trade = nullptr;

        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t c0 = rdtsc_now();

        for (int i = 0; i < count; ++i) {
            if (i % 2 == 0)
                book->add_limit_order(static_cast<uint64_t>(i * 2),     Side::BUY,  10000, 100);
            else
                book->add_limit_order(static_cast<uint64_t>(i * 2 + 1), Side::SELL, 10000, 100);
        }

        uint64_t c1 = rdtsc_now();
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < best_ms) {
            best_ms = ms;
            best_cycles = c1 - c0;
            trades = book->total_trades();
        }
    }

    return { best_ms, best_cycles, trades, static_cast<uint64_t>(count) };
}

static TimingResult bench_matching_only_naive(int count, int runs) {
    double best_ms = 1e18;
    uint64_t best_cycles = UINT64_MAX;
    uint64_t trades = 0;

    for (int r = 0; r < runs; ++r) {
        auto book = std::make_unique<NaiveOrderBook>();
        book->on_trade = nullptr;

        auto t0 = std::chrono::high_resolution_clock::now();
        uint64_t c0 = rdtsc_now();

        for (int i = 0; i < count; ++i) {
            if (i % 2 == 0)
                book->add_limit_order(static_cast<uint64_t>(i * 2),     Side::BUY,  10000, 100);
            else
                book->add_limit_order(static_cast<uint64_t>(i * 2 + 1), Side::SELL, 10000, 100);
        }

        uint64_t c1 = rdtsc_now();
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < best_ms) {
            best_ms = ms;
            best_cycles = c1 - c0;
            trades = book->total_trades();
        }
    }

    return { best_ms, best_cycles, trades, static_cast<uint64_t>(count) };
}

// ─── Print helpers ───────────────────────────────────────────────────────────

static void print_separator() {
    printf("------------------------------------------------------------------------\n");
}

static void print_row(const char* label, double opt, double naive, const char* unit) {
    double speedup = (opt > 0) ? naive / opt : 0;
    printf("  %-28s %12.1f %12.1f %10.1fx  %s\n",
           label, opt, naive, speedup, unit);
}

static void print_row_int(const char* label, uint64_t opt, uint64_t naive) {
    printf("  %-28s %12llu %12llu %10s\n",
           label,
           (unsigned long long)opt, (unsigned long long)naive,
           (opt == naive) ? "(match)" : "MISMATCH!");
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* path = argc > 1 ? argv[1] : "data/small.csv";
    int runs = argc > 2 ? atoi(argv[2]) : 3;

    printf("\n");
    printf("========================================================================\n");
    printf("  NanoMatch Benchmark — Optimized vs Naive STL Baseline\n");
    printf("========================================================================\n\n");

    // ── Load data ────────────────────────────────────────────────────────────
    printf("  Loading %s ...\n", path);
    auto orders = load_csv(path);
    if (orders.empty()) {
        fprintf(stderr, "  ERROR: No orders loaded from %s\n", path);
        fprintf(stderr, "  Generate data: python3 data/gen_orders.py -n 500000 -o data/orders_500k.csv\n");
        return 1;
    }
    printf("  Loaded %zu orders, running %d iterations (best-of)\n\n", orders.size(), runs);

    // ── Calibrate TSC ────────────────────────────────────────────────────────
    printf("  Calibrating TSC...\n");
    double ns_per_tick = calibrate_tsc_ns();
    printf("  TSC calibration: %.3f ns/tick (%.1f MHz)\n\n", ns_per_tick, 1000.0 / ns_per_tick);

    // ── Throughput benchmark ─────────────────────────────────────────────────
    printf("  [1/3] Throughput benchmark (%zu orders x %d runs)...\n", orders.size(), runs);
    fflush(stdout);

    TimingResult opt = bench_optimized(orders, runs);
    printf("        Optimized: %.2f ms\n", opt.wall_ms);
    fflush(stdout);

    TimingResult naive = bench_naive(orders, runs);
    printf("        Naive:     %.2f ms\n\n", naive.wall_ms);
    fflush(stdout);

    // ── Latency benchmark ────────────────────────────────────────────────────
    printf("  [2/3] Per-order latency benchmark (second half of dataset)...\n");
    fflush(stdout);

    LatencyStats opt_lat = latency_optimized(orders, ns_per_tick);
    printf("        Optimized latency measured\n");
    fflush(stdout);

    LatencyStats naive_lat = latency_naive(orders, ns_per_tick);
    printf("        Naive latency measured\n\n");
    fflush(stdout);

    // ── Match-only benchmark ─────────────────────────────────────────────────
    constexpr int MATCH_COUNT = 100'000;
    printf("  [3/3] Match-only benchmark (%d alternating orders x %d runs)...\n",
           MATCH_COUNT, runs);
    fflush(stdout);

    TimingResult match_opt = bench_matching_only_optimized(MATCH_COUNT, runs);
    printf("        Optimized: %.2f ms\n", match_opt.wall_ms);
    fflush(stdout);

    TimingResult match_naive = bench_matching_only_naive(MATCH_COUNT, runs);
    printf("        Naive:     %.2f ms\n\n", match_naive.wall_ms);
    fflush(stdout);

    // ── Results table ────────────────────────────────────────────────────────
    printf("========================================================================\n");
    printf("  RESULTS — %zu orders, best of %d runs\n", orders.size(), runs);
    printf("========================================================================\n\n");

    double opt_mops   = (static_cast<double>(opt.orders) / opt.wall_ms) / 1000.0;
    double naive_mops = (static_cast<double>(naive.orders) / naive.wall_ms) / 1000.0;
    double opt_cpo    = static_cast<double>(opt.cycles) / static_cast<double>(opt.orders);
    double naive_cpo  = static_cast<double>(naive.cycles) / static_cast<double>(naive.orders);
    double match_opt_mops  = (static_cast<double>(match_opt.orders) / match_opt.wall_ms) / 1000.0;
    double match_naive_mops = (static_cast<double>(match_naive.orders) / match_naive.wall_ms) / 1000.0;

    printf("  %-28s %12s %12s %10s\n", "Metric", "Optimized", "Naive", "Speedup");
    print_separator();

    // Wall time (lower = better → speedup = naive/opt)
    print_row("Wall time (ms)", opt.wall_ms, naive.wall_ms, "");

    // Cycles per order (lower = better)
    print_row("Cycles/order", opt_cpo, naive_cpo, "");

    // Throughput (higher = better → speedup = opt/naive)
    printf("  %-28s %12.2f %12.2f %10.1fx  %s\n",
           "Throughput (M ord/s)", opt_mops, naive_mops,
           opt_mops / (naive_mops > 0 ? naive_mops : 1.0), "(higher=better)");

    print_separator();

    // Latency percentiles (lower = better)
    print_row("p50 latency (ns)",  opt_lat.p50_ns,  naive_lat.p50_ns,  "");
    print_row("p90 latency (ns)",  opt_lat.p90_ns,  naive_lat.p90_ns,  "");
    print_row("p99 latency (ns)",  opt_lat.p99_ns,  naive_lat.p99_ns,  "");
    print_row("p99.9 latency (ns)", opt_lat.p999_ns, naive_lat.p999_ns, "");
    print_row("Mean latency (ns)", opt_lat.mean_ns, naive_lat.mean_ns, "");

    print_separator();

    // Match-only throughput (higher = better)
    printf("  %-28s %12.2f %12.2f %10.1fx  %s\n",
           "Match-only (M ord/s)", match_opt_mops, match_naive_mops,
           match_opt_mops / (match_naive_mops > 0 ? match_naive_mops : 1.0),
           "(higher=better)");

    print_separator();

    // Correctness check
    print_row_int("Trades (throughput)", opt.trades, naive.trades);
    print_row_int("Trades (match-only)", match_opt.trades, match_naive.trades);

    printf("\n========================================================================\n");

    if (opt.trades != naive.trades) {
        printf("  WARNING: Trade count mismatch! Engines produce different results.\n");
    } else {
        printf("  Correctness: PASS — both engines produced identical trade counts.\n");
    }

    printf("========================================================================\n\n");

    // ── Summary for README paste ─────────────────────────────────────────────
    printf("── Copy-paste for README.md ────────────────────────────────────────\n\n");
    printf("| Metric | Optimized | Naive (std::map) | Speedup |\n");
    printf("|---|---|---|---|\n");
    printf("| Throughput | %.2f M orders/sec | %.2f M orders/sec | **%.1fx** |\n",
           opt_mops, naive_mops, opt_mops / (naive_mops > 0 ? naive_mops : 1.0));
    printf("| p50 latency | %.0f ns | %.0f ns | %.1fx |\n",
           opt_lat.p50_ns, naive_lat.p50_ns,
           naive_lat.p50_ns / (opt_lat.p50_ns > 0 ? opt_lat.p50_ns : 1.0));
    printf("| p99 latency | %.0f ns | %.0f ns | %.1fx |\n",
           opt_lat.p99_ns, naive_lat.p99_ns,
           naive_lat.p99_ns / (opt_lat.p99_ns > 0 ? opt_lat.p99_ns : 1.0));
    printf("| Cycles/order | %.0f | %.0f | %.1fx |\n",
           opt_cpo, naive_cpo, naive_cpo / (opt_cpo > 0 ? opt_cpo : 1.0));
    printf("| Trades | %llu | %llu | %s |\n",
           (unsigned long long)opt.trades, (unsigned long long)naive.trades,
           (opt.trades == naive.trades) ? "✓ match" : "✗ MISMATCH");
    printf("\n");

    return 0;
}
