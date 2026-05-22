// nanomatch_demo — streams JSON snapshots to stdout for the web visualizer
// JSON lines protocol (one JSON object per line):
//   {"type":"snapshot","bids":[...],"asks":[...],"last_trade":null|{...},"stats":{...}}
//   {"type":"done","stats":{...}}

#include "order_book.hpp"
#include "csv_parser.hpp"
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <memory>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
static inline uint64_t rdtsc_d() { return __rdtsc(); }
#else
static inline uint64_t rdtsc_d() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

static void print_json_snapshot(const OrderBook& book,
                                 bool has_trade, uint64_t tprice, uint64_t tqty, char tside,
                                 uint64_t processed, uint64_t trades, double throughput) {
    auto bids = book.bid_levels(10);
    auto asks = book.ask_levels(10);

    printf("{\"type\":\"snapshot\",");

    // bids
    printf("\"bids\":[");
    for (size_t i = 0; i < bids.size(); ++i) {
        if (i) printf(",");
        printf("{\"price\":%.2f,\"qty\":%llu,\"count\":%u}",
               bids[i].price / 100.0,
               (unsigned long long)bids[i].qty,
               bids[i].count);
    }
    printf("],");

    // asks
    printf("\"asks\":[");
    for (size_t i = 0; i < asks.size(); ++i) {
        if (i) printf(",");
        printf("{\"price\":%.2f,\"qty\":%llu,\"count\":%u}",
               asks[i].price / 100.0,
               (unsigned long long)asks[i].qty,
               asks[i].count);
    }
    printf("],");

    // last_trade
    if (has_trade) {
        printf("\"last_trade\":{\"price\":%.2f,\"qty\":%llu,\"side\":\"%s\"},",
               tprice / 100.0, (unsigned long long)tqty,
               tside == 'B' ? "BUY" : "SELL");
    } else {
        printf("\"last_trade\":null,");
    }

    // stats
    double spread = 0.0;
    if (book.best_bid() > 0 && book.best_ask() != UINT64_MAX)
        spread = (book.best_ask() - book.best_bid()) / 100.0;

    printf("\"stats\":{"
           "\"processed\":%llu,"
           "\"trades\":%llu,"
           "\"throughput\":%.2f,"
           "\"best_bid\":%.2f,"
           "\"best_ask\":%.2f,"
           "\"spread\":%.2f"
           "}}\n",
           (unsigned long long)processed,
           (unsigned long long)trades,
           throughput,
           book.best_bid() > 0 ? book.best_bid() / 100.0 : 0.0,
           book.best_ask() != UINT64_MAX ? book.best_ask() / 100.0 : 0.0,
           spread);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    const char* path      = argc > 1 ? argv[1] : "data/orders_500k.csv";
    int         speed     = argc > 2 ? atoi(argv[2]) : 80;   // orders/sec for visual demo
    int         snap_every = argc > 3 ? atoi(argv[3]) : 5;   // emit snapshot every N orders

    fprintf(stderr, "[demo] Loading %s at %d orders/sec...\n", path, speed);
    auto orders = load_csv(path);
    if (orders.empty()) {
        fprintf(stderr, "[demo] ERROR: no orders loaded\n");
        return 1;
    }
    fprintf(stderr, "[demo] Loaded %zu orders. Starting stream...\n", orders.size());

    auto book = std::make_unique<OrderBook>();

    uint64_t trades = 0;
    bool     has_trade = false;
    uint64_t last_tprice = 0, last_tqty = 0;
    char     last_tside  = 'B';

    book->on_trade = [&](const Trade& t) {
        ++trades;
        has_trade   = true;
        last_tprice = t.price;
        last_tqty   = t.quantity;
        last_tside  = 'B'; // trade is always a buy consuming a sell
    };

    const auto interval_us = std::chrono::microseconds(1'000'000 / speed);
    auto t_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < orders.size(); ++i) {
        const auto& m = orders[i];

        switch (m.type) {
            case OrderType::LIMIT:  book->add_limit_order(m.order_id, m.side, m.price, m.quantity); break;
            case OrderType::MARKET: book->add_market_order(m.order_id, m.side, m.quantity); break;
            case OrderType::CANCEL: book->cancel_order(m.price); break;
        }

        if ((int)(i % snap_every) == 0) {
            auto elapsed = std::chrono::high_resolution_clock::now() - t_start;
            double secs  = std::chrono::duration<double>(elapsed).count();
            double tp    = secs > 0 ? (i + 1) / secs : 0.0;

            print_json_snapshot(*book, has_trade,
                                last_tprice, last_tqty, last_tside,
                                i + 1, trades, tp);
            has_trade = false; // reset per-snapshot

            // Throttle for human-readable demo speed
            std::this_thread::sleep_for(interval_us);
        }
    }

    // Final snapshot
    auto elapsed = std::chrono::high_resolution_clock::now() - t_start;
    double secs  = std::chrono::duration<double>(elapsed).count();
    printf("{\"type\":\"done\",\"stats\":{\"processed\":%zu,\"trades\":%llu,\"throughput\":%.2f}}\n",
           orders.size(), (unsigned long long)trades, orders.size() / secs);
    fflush(stdout);
    return 0;
}
