// Standalone tests — no external dependencies
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include <cstdio>
#include <memory>
#include <vector>
#include <cassert>
#include <functional>
#include <thread>
#include <atomic>

struct TestResult { int passed=0, failed=0; };
static TestResult g_result;

#define TEST(name) void test_##name()
#define RUN(name) do { \
    printf("  %-45s", #name); fflush(stdout); \
    try { test_##name(); printf("PASS\n"); g_result.passed++; } \
    catch(const char* msg) { printf("FAIL: %s\n", msg); g_result.failed++; } \
} while(0)

#define EXPECT_EQ(a,b) do { \
    auto _a=(a); auto _b=(b); \
    if(_a!=_b){ char buf[256]; \
        snprintf(buf,256,"Expected %llu == %llu (%s == %s)", \
                 (unsigned long long)_a,(unsigned long long)_b,#a,#b); \
        throw (const char*)buf; } \
} while(0)
#define EXPECT_TRUE(x)  do { if(!(x)) throw #x " is false"; } while(0)
#define EXPECT_FALSE(x) do { if( (x)) throw #x " is true";  } while(0)
#define EXPECT_NO_THROW(expr) do { try { expr; } catch(...) { throw "threw unexpectedly"; } } while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────
struct Ctx {
    std::unique_ptr<OrderBook> book;
    std::vector<Trade> trades;
    Ctx() : book(std::make_unique<OrderBook>()) {
        book->on_trade = [this](const Trade& t){ trades.push_back(t); };
    }
    uint64_t filled() { uint64_t s=0; for(auto&t:trades) s+=t.quantity; return s; }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(FullFill) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY,  10000, 100);
    c.book->add_limit_order(2, Side::SELL, 10000, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].quantity, 100u);
    EXPECT_EQ(c.trades[0].price, 10000u);
    EXPECT_EQ(c.book->best_bid(), 0u);
    EXPECT_EQ(c.book->best_ask(), UINT64_MAX);
}

TEST(PartialFill_BuyLarger) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY,  10000, 100);
    c.book->add_limit_order(2, Side::SELL, 10000, 60);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].quantity, 60u);
    EXPECT_EQ(c.book->best_bid(), 10000u);
    EXPECT_EQ(c.book->bid_qty_at(10000), 40u);
    EXPECT_EQ(c.book->best_ask(), UINT64_MAX);
}

TEST(PartialFill_SellLarger) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY,  10000, 60);
    c.book->add_limit_order(2, Side::SELL, 10000, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].quantity, 60u);
    EXPECT_EQ(c.book->best_ask(), 10000u);
    EXPECT_EQ(c.book->ask_qty_at(10000), 40u);
    EXPECT_EQ(c.book->best_bid(), 0u);
}

TEST(PriceTimePriority) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY, 10000, 50);  // arrives first
    c.book->add_limit_order(2, Side::BUY, 10000, 50);
    c.book->add_limit_order(3, Side::SELL,10000, 50);   // must fill order 1
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].buy_order_id, 1u);
    EXPECT_EQ(c.book->best_bid(), 10000u);
    EXPECT_EQ(c.book->bid_qty_at(10000), 50u);
}

TEST(PricePriority_BestFirst) {
    Ctx c;
    c.book->add_limit_order(1, Side::SELL, 10000, 100);
    c.book->add_limit_order(2, Side::SELL,  9900, 100); // better ask
    c.book->add_limit_order(3, Side::BUY,  10000, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].sell_order_id, 2u);
    EXPECT_EQ(c.trades[0].price, 9900u);
}

TEST(MultiLevelSweep) {
    Ctx c;
    c.book->add_limit_order(1, Side::SELL,  9900, 10);
    c.book->add_limit_order(2, Side::SELL, 10000, 10);
    c.book->add_limit_order(3, Side::SELL, 10100, 10);
    c.book->add_market_order(4, Side::BUY, 25);
    EXPECT_EQ(c.trades.size(), 3u);
    EXPECT_EQ(c.filled(), 25u);
    EXPECT_EQ(c.book->best_ask(), 10100u);
    EXPECT_EQ(c.book->ask_qty_at(10100), 5u);
}

TEST(MarketBuy_FullFill) {
    Ctx c;
    c.book->add_limit_order(1, Side::SELL, 10000, 100);
    c.book->add_market_order(2, Side::BUY, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].quantity, 100u);
    EXPECT_EQ(c.book->best_ask(), UINT64_MAX);
}

TEST(MarketOrder_NoLiquidity) {
    Ctx c;
    EXPECT_NO_THROW(c.book->add_market_order(1, Side::BUY, 100));
    EXPECT_TRUE(c.trades.empty());
}

TEST(MarketSell_FullFill) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY, 10000, 100);
    c.book->add_market_order(2, Side::SELL, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.book->best_bid(), 0u);
}

TEST(Cancel_PreventsFill) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY, 10000, 100);
    c.book->cancel_order(1);
    c.book->add_limit_order(2, Side::SELL, 10000, 100);
    EXPECT_TRUE(c.trades.empty());
    EXPECT_EQ(c.book->best_bid(), 0u);
    EXPECT_EQ(c.book->best_ask(), 10000u);
}

TEST(Cancel_NonExistent) {
    Ctx c;
    EXPECT_NO_THROW(c.book->cancel_order(99999));
}

TEST(Cancel_PartialBook) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY, 10000, 100);
    c.book->add_limit_order(2, Side::BUY, 10000,  50);
    c.book->cancel_order(1);
    EXPECT_EQ(c.book->best_bid(), 10000u);
    EXPECT_EQ(c.book->bid_qty_at(10000), 50u);
}

TEST(CrossedBook_Resolves) {
    Ctx c;
    c.book->add_limit_order(1, Side::BUY,  10100, 100);
    c.book->add_limit_order(2, Side::SELL,  9900, 100);
    EXPECT_EQ(c.trades.size(), 1u);
    EXPECT_EQ(c.trades[0].quantity, 100u);
    EXPECT_EQ(c.trades[0].price, 9900u);
}

TEST(LargeVolume_PoolRecycling) {
    Ctx c;
    for (uint64_t i = 0; i < 100'000; ++i) {
        if (i % 2 == 0) c.book->add_limit_order(i*2,   Side::BUY,  10000, 1);
        else             c.book->add_limit_order(i*2+1, Side::SELL, 10000, 1);
    }
    EXPECT_EQ(c.book->total_trades(), 50'000u);
}

TEST(OutOfRangePrice_Ignored) {
    Ctx c;
    EXPECT_NO_THROW(c.book->add_limit_order(1, Side::BUY, 0, 100));
    EXPECT_EQ(c.book->best_bid(), 0u);
}

// ── Lock-free SPSC trade queue ─────────────────────────────────────────────────
// These exercise the lock-free ring buffer that the matching engine uses to
// hand trades to the logger thread without locks or syscalls in the hot path.

static Trade make_trade(uint64_t seq) {
    return Trade{ /*buy_order_id*/ seq,
                  /*sell_order_id*/ seq + 1,
                  /*price*/ 10000 + (seq % 100),
                  /*quantity*/ 1 + (seq % 50),
                  /*timestamp*/ seq };
}

// Push then drain on one thread — proves FIFO ordering and that an empty
// queue reports nothing left.
TEST(SPSC_SingleThread_FIFO) {
    auto q = std::make_unique<TradeQueue>();
    constexpr uint64_t N = 10'000;

    for (uint64_t i = 0; i < N; ++i)
        EXPECT_TRUE(q->push(make_trade(i)));

    for (uint64_t i = 0; i < N; ++i) {
        auto t = q->pop();
        EXPECT_TRUE(t.has_value());
        EXPECT_EQ(t->buy_order_id, i);          // strict FIFO order
        EXPECT_EQ(t->quantity, 1 + (i % 50));   // payload intact
    }
    EXPECT_FALSE(q->pop().has_value());         // fully drained
    EXPECT_TRUE(q->empty());
}

// A full ring buffer must reject pushes (no overwrite), and free a slot on pop.
// Usable capacity is N-1 because head==tail encodes "empty".
TEST(SPSC_FullQueue_Backpressure) {
    SPSCQueue<Trade, 4> q;                      // capacity = 3 usable slots
    EXPECT_TRUE(q.push(make_trade(0)));
    EXPECT_TRUE(q.push(make_trade(1)));
    EXPECT_TRUE(q.push(make_trade(2)));
    EXPECT_FALSE(q.push(make_trade(3)));        // full → rejected, not dropped silently

    auto t = q.pop();                           // free one slot
    EXPECT_TRUE(t.has_value());
    EXPECT_EQ(t->buy_order_id, 0u);
    EXPECT_TRUE(q.push(make_trade(3)));         // now there is room
}

// The real proof: a producer thread and a consumer thread run concurrently,
// exactly like the engine + logger. Every trade must arrive exactly once, in
// order, with no loss — relying solely on the queue's atomic acquire/release.
TEST(SPSC_ConcurrentProducerConsumer) {
    auto q = std::make_unique<TradeQueue>();
    constexpr uint64_t N = 500'000;

    std::vector<Trade> received;
    received.reserve(N);

    std::thread consumer([&] {
        uint64_t got = 0;
        while (got < N) {
            auto t = q->pop();
            if (t) { received.push_back(*t); ++got; }
            // else: spin — queue momentarily empty
        }
    });

    // Producer: push all N, spinning if the consumer hasn't drained yet.
    for (uint64_t i = 0; i < N; ++i)
        while (!q->push(make_trade(i)))
            std::this_thread::yield();

    consumer.join();

    EXPECT_EQ((uint64_t)received.size(), N);    // none lost, none duplicated
    bool ordered = true;
    for (uint64_t i = 0; i < N; ++i)
        if (received[i].buy_order_id != i) { ordered = false; break; }
    EXPECT_TRUE(ordered);                        // FIFO preserved across threads
    EXPECT_TRUE(q->empty());
}

int main() {
    printf("\n── NanoMatch Test Suite ────────────────────────────\n");
    RUN(FullFill);
    RUN(PartialFill_BuyLarger);
    RUN(PartialFill_SellLarger);
    RUN(PriceTimePriority);
    RUN(PricePriority_BestFirst);
    RUN(MultiLevelSweep);
    RUN(MarketBuy_FullFill);
    RUN(MarketOrder_NoLiquidity);
    RUN(MarketSell_FullFill);
    RUN(Cancel_PreventsFill);
    RUN(Cancel_NonExistent);
    RUN(Cancel_PartialBook);
    RUN(CrossedBook_Resolves);
    RUN(LargeVolume_PoolRecycling);
    RUN(OutOfRangePrice_Ignored);
    RUN(SPSC_SingleThread_FIFO);
    RUN(SPSC_FullQueue_Backpressure);
    RUN(SPSC_ConcurrentProducerConsumer);
    printf("────────────────────────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", g_result.passed, g_result.failed);
    return g_result.failed > 0 ? 1 : 0;
}
