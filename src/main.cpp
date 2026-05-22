#include "order_book.hpp"
#include "csv_parser.hpp"
#include "logger.hpp"
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <memory>

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

int main(int argc, char* argv[]) {
    const char* data_path  = argc > 1 ? argv[1] : "data/orders_1m.csv";
    const char* trade_path = argc > 2 ? argv[2] : "data/trades_out.csv";

    // ── Load orders ──────────────────────────────────────────────────────────
    printf("Loading orders from %s ...\n", data_path);
    auto orders = load_csv(data_path);
    if (orders.empty()) {
        fprintf(stderr, "No orders loaded. Generate data first:\n");
        fprintf(stderr, "  python3 data/gen_orders.py -n 1000000\n");
        return 1;
    }
    printf("Loaded %zu orders\n", orders.size());

    // ── Set up SPSC queue + logger thread ───────────────────────────────────
    auto tq_ptr = std::make_unique<TradeQueue>();
    TradeQueue& tq = *tq_ptr;
    TradeLogger logger(tq, trade_path, /*cpu_core=*/1);

    // ── Set up order book (heap-allocated — too large for stack) ────────────
    auto book_ptr = std::make_unique<OrderBook>();
    OrderBook& book = *book_ptr;
    book.on_trade = [&tq](const Trade& t) {
        // Non-blocking push — if queue full, trade is dropped
        // In production: handle backpressure properly
        tq.push(t);
    };

    // ── Replay + time it ────────────────────────────────────────────────────
    printf("Replaying %zu orders ...\n", orders.size());

    auto wall_start = std::chrono::high_resolution_clock::now();
    uint64_t tsc_start = rdtsc();

    replay(book, orders);

    uint64_t tsc_end = rdtsc();
    auto wall_end = std::chrono::high_resolution_clock::now();

    double wall_ms = std::chrono::duration<double, std::milli>(
                         wall_end - wall_start).count();
    uint64_t cycles = tsc_end - tsc_start;

    // ── Results ──────────────────────────────────────────────────────────────
    printf("\n── NanoMatch Results ──────────────────────────────\n");
    printf("Orders processed : %zu\n",   orders.size());
    printf("Trades generated : %llu\n",  (unsigned long long)book.total_trades());
    printf("Cancels processed: %llu\n",  (unsigned long long)book.total_cancels());
    printf("Wall time        : %.2f ms\n", wall_ms);
    printf("Throughput       : %.2f M orders/sec\n",
           (orders.size() / wall_ms) / 1000.0);
    printf("Cycles total     : %llu\n",   (unsigned long long)cycles);
    printf("Cycles/order     : %.1f\n",
           static_cast<double>(cycles) / orders.size());
    printf("Best Bid         : %llu ticks ($%.2f)\n",
           (unsigned long long)book.best_bid(),
           book.best_bid() / 100.0);
    printf("Best Ask         : ");
    if (book.best_ask() == UINT64_MAX) printf("empty\n");
    else printf("%llu ticks ($%.2f)\n",
                (unsigned long long)book.best_ask(),
                book.best_ask() / 100.0);
    printf("───────────────────────────────────────────────────\n");
    printf("Trades logged to : %s\n", trade_path);

    return 0;
}
