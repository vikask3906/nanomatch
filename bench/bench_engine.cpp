#include <benchmark/benchmark.h>
#include "order_book.hpp"
#include "csv_parser.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>

// ── Load once, reuse across all benchmarks ───────────────────────────────────
static std::vector<OrderMsg> g_orders;

static void load_global_orders() {
    if (!g_orders.empty()) return;
    g_orders = load_csv("data/orders_1m.csv");
    if (g_orders.empty()) {
        // Fallback: generate synthetic orders in-memory if CSV missing
        uint64_t id = 1;
        for (int i = 0; i < 1'000'000; ++i) {
            OrderMsg m{};
            m.order_id = id++;
            int r = i % 10;
            if (r < 7) {
                m.type     = OrderType::LIMIT;
                m.side     = (i % 2 == 0) ? Side::BUY : Side::SELL;
                m.price    = 10000 + (i % 100) - 50;  // cluster around $100
                m.quantity = 10 + (i % 90);
            } else if (r < 9) {
                m.type     = OrderType::MARKET;
                m.side     = (i % 2 == 0) ? Side::BUY : Side::SELL;
                m.quantity = 50;
            } else {
                m.type  = OrderType::CANCEL;
                m.price = (id > 2) ? id - 2 : 1;  // cancel a recent order
            }
            g_orders.push_back(m);
        }
    }
}

// ── Throughput benchmark ─────────────────────────────────────────────────────

static void BM_OrderBook_Throughput(benchmark::State& state) {
    load_global_orders();
    for (auto _ : state) {
        OrderBook book;
        // Silence trade callback for pure throughput measurement
        book.on_trade = nullptr;
        replay(book, g_orders);
        benchmark::DoNotOptimize(book.total_trades());
    }
    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) *
        static_cast<int64_t>(g_orders.size()));
    state.SetLabel("orders/sec");
}

BENCHMARK(BM_OrderBook_Throughput)
    ->Iterations(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kMillisecond);

// ── Per-order latency histogram ──────────────────────────────────────────────

static void BM_SingleOrder_Latency(benchmark::State& state) {
    load_global_orders();
    OrderBook book;
    book.on_trade = nullptr;

    // Pre-warm the book with half the orders so the book is in a realistic state
    size_t warmup = g_orders.size() / 2;
    for (size_t i = 0; i < warmup; ++i) {
        const auto& m = g_orders[i];
        switch (m.type) {
            case OrderType::LIMIT:  book.add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book.add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book.cancel_order(m.price); break;
        }
    }

    size_t idx = warmup;
    for (auto _ : state) {
        if (idx >= g_orders.size()) idx = warmup;
        const auto& m = g_orders[idx++];
        switch (m.type) {
            case OrderType::LIMIT:
                benchmark::DoNotOptimize(
                    book.add_limit_order(m.order_id, m.side, m.price, m.quantity));
                break;
            case OrderType::MARKET:
                benchmark::DoNotOptimize(
                    book.add_market_order(m.order_id, m.side, m.quantity));
                break;
            case OrderType::CANCEL:
                benchmark::DoNotOptimize(book.cancel_order(m.price));
                break;
        }
    }
    state.SetLabel("ns/order");
}

BENCHMARK(BM_SingleOrder_Latency)
    ->Iterations(1'000'000)
    ->ReportAggregatesOnly(false)
    ->Unit(benchmark::kNanosecond);

// ── Matching-only benchmark (limit orders with guaranteed matches) ────────────

static void BM_MatchingOnly(benchmark::State& state) {
    for (auto _ : state) {
        OrderBook book;
        book.on_trade = nullptr;
        // Alternating buy/sell at same price — every order matches immediately
        for (uint64_t i = 0; i < 100'000; ++i) {
            if (i % 2 == 0) book.add_limit_order(i*2,   Side::BUY,  10000, 100);
            else             book.add_limit_order(i*2+1, Side::SELL, 10000, 100);
        }
        benchmark::DoNotOptimize(book.total_trades());
    }
    state.SetItemsProcessed(state.iterations() * 100'000);
}

BENCHMARK(BM_MatchingOnly)
    ->Iterations(20)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
