#pragma once
#include "order.hpp"
#include "price_level.hpp"
#include "memory_pool.hpp"
#include "spsc_queue.hpp"
#include <unordered_map>
#include <set>
#include <cstdint>
#include <functional>

class OrderBook {
public:
    static constexpr uint64_t MIN_PRICE =     1;
    static constexpr uint64_t MAX_PRICE = 200'000;
    static constexpr uint64_t LEVELS    = MAX_PRICE - MIN_PRICE + 1;

    using TradeCallback = std::function<void(const Trade&)>;
    TradeCallback on_trade;

    OrderBook();
    ~OrderBook();

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    void add_limit_order (uint64_t id, Side side, uint64_t price, uint64_t qty);
    void add_market_order(uint64_t id, Side side, uint64_t qty);
    void cancel_order    (uint64_t id);

    uint64_t best_bid() const { return best_bid_; }
    uint64_t best_ask() const { return best_ask_; }
    uint64_t bid_qty_at(uint64_t price) const;
    uint64_t ask_qty_at(uint64_t price) const;

    uint64_t total_trades()  const { return trade_count_; }
    uint64_t total_orders()  const { return order_count_; }
    uint64_t total_cancels() const { return cancel_count_; }

    EnginePool& pool() { return pool_; }

private:
    EnginePool pool_;

    PriceLevel* bids_;
    PriceLevel* asks_;

    uint64_t best_bid_ = 0;
    uint64_t best_ask_ = UINT64_MAX;

    // Active price sets — O(log n) best bid/ask update, no full scan
    std::set<uint64_t> active_bids_;
    std::set<uint64_t> active_asks_;

    std::unordered_map<uint64_t, uint32_t> id_to_idx_;

    uint64_t trade_count_  = 0;
    uint64_t order_count_  = 0;
    uint64_t cancel_count_ = 0;

    void match_buy (Order* incoming);
    void match_sell(Order* incoming);
    void rest_order(Order* o);
    void update_best_bid();
    void update_best_ask();
    void emit_trade(Order* buy, Order* sell, uint64_t qty);

    inline PriceLevel& bid_level(uint64_t price) {
        return bids_[price - MIN_PRICE];
    }
    inline PriceLevel& ask_level(uint64_t price) {
        return asks_[price - MIN_PRICE];
    }

    static inline uint64_t now_tsc() {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
};
