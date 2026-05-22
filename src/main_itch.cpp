#include "order_book.hpp"
#include "itch_parser.hpp"
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <memory>

#if defined(_MSC_VER)
#include <intrin.h>
static inline uint64_t rdtsc() { return __rdtsc(); }
#else
static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

int main(int argc, char* argv[]) {
    const char* data_path = argc > 1 ? argv[1] : "data/orders_500k.itch";

    printf("Loading ITCH binary %s ...\n", data_path);
    auto t_load_start = std::chrono::high_resolution_clock::now();
    
    auto orders = load_itch(data_path);
    
    auto t_load_end = std::chrono::high_resolution_clock::now();
    double load_ms = std::chrono::duration<double, std::milli>(t_load_end - t_load_start).count();
    
    if (orders.empty()) { fprintf(stderr, "No orders loaded\n"); return 1; }
    printf("Loaded %zu orders in %.2f ms via zero-copy mmap\n", orders.size(), load_ms);

    auto book = std::make_unique<OrderBook>();
    uint64_t trade_count = 0;
    book->on_trade = [&](const Trade& t) {
        ++trade_count;
        (void)t;
    };

    auto t0 = std::chrono::high_resolution_clock::now();
    uint64_t c0 = rdtsc();

    replay(*book, orders);

    uint64_t c1 = rdtsc();
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    printf("\n── NanoMatch ITCH Results ────────────────────────\n");
    printf("Orders processed : %zu\n",   orders.size());
    printf("Trades generated : %llu\n",  (unsigned long long)trade_count);
    printf("Cancels          : %llu\n",  (unsigned long long)book->total_cancels());
    printf("Wall time        : %.2f ms\n", ms);
    printf("Throughput       : %.2f M orders/sec\n", (orders.size() / ms) / 1000.0);
    printf("Cycles/order     : %.1f\n",   (double)(c1-c0) / orders.size());
    printf("Best Bid         : $%.2f\n",  book->best_bid() / 100.0);
    if (book->best_ask() == UINT64_MAX)
        printf("Best Ask         : empty\n");
    else
        printf("Best Ask         : $%.2f\n", book->best_ask() / 100.0);
    printf("──────────────────────────────────────────────────\n");
    return 0;
}
