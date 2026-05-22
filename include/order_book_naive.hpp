#pragma once
// ─── NaiveOrderBook — STL baseline for benchmarking comparison ───────────────
//
// This is a DELIBERATELY SLOW implementation of a matching engine using
// standard library containers. It exists solely to demonstrate the performance
// gap vs. the optimized engine.
//
// Slow patterns used on purpose:
//   - std::map<price, std::deque<Order*>>  → O(log n) price lookup, pointer
//     chasing through red-black tree nodes, scattered heap allocations
//   - new Order / delete Order             → malloc/free on every order,
//     heap fragmentation, potential brk()/mmap() syscalls
//   - std::deque<Order*>                   → double indirection to reach
//     order data (deque chunk → pointer → Order)
//   - std::unordered_map<id, Order*>       → hash table for cancel lookup
//     (same as optimized, but orders themselves are heap-scattered)
//
// Interface is identical to OrderBook so both can be driven by replay().
// ─────────────────────────────────────────────────────────────────────────────

#include "order.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <chrono>

class NaiveOrderBook {
public:
    static constexpr uint64_t MIN_PRICE =     1;
    static constexpr uint64_t MAX_PRICE = 200'000;

    using TradeCallback = std::function<void(const Trade&)>;
    TradeCallback on_trade;

    NaiveOrderBook() = default;

    ~NaiveOrderBook() {
        // Clean up all heap-allocated orders
        for (auto& [price, queue] : bids_) {
            for (Order* o : queue) delete o;
        }
        for (auto& [price, queue] : asks_) {
            for (Order* o : queue) delete o;
        }
    }

    NaiveOrderBook(const NaiveOrderBook&) = delete;
    NaiveOrderBook& operator=(const NaiveOrderBook&) = delete;

    void add_limit_order(uint64_t id, Side side, uint64_t price, uint64_t qty) {
        if (price < MIN_PRICE || price > MAX_PRICE) return;
        ++order_count_;

        Order* o   = new Order();      // malloc every time — the whole point
        o->order_id = id;
        o->price    = price;
        o->quantity = qty;
        o->side     = side;
        o->type     = OrderType::LIMIT;
        o->timestamp = now_ns();

        if (side == Side::BUY)  match_buy(o);
        else                    match_sell(o);
    }

    void add_market_order(uint64_t id, Side side, uint64_t qty) {
        ++order_count_;

        Order* o    = new Order();
        o->order_id = id;
        o->quantity = qty;
        o->side     = side;
        o->type     = OrderType::MARKET;
        o->timestamp = now_ns();

        if (side == Side::BUY)  { o->price = UINT64_MAX; match_buy(o);  }
        else                    { o->price = 0;           match_sell(o); }
    }

    void cancel_order(uint64_t id) {
        auto it = id_to_order_.find(id);
        if (it == id_to_order_.end()) return;
        Order* o = it->second;
        o->cancelled = true;
        id_to_order_.erase(it);
        ++cancel_count_;
    }

    uint64_t best_bid() const {
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
            for (const Order* o : it->second) {
                if (!o->cancelled && o->quantity > 0) return it->first;
            }
        }
        return 0;
    }

    uint64_t best_ask() const {
        for (auto it = asks_.begin(); it != asks_.end(); ++it) {
            for (const Order* o : it->second) {
                if (!o->cancelled && o->quantity > 0) return it->first;
            }
        }
        return UINT64_MAX;
    }

    uint64_t bid_qty_at(uint64_t price) const {
        auto it = bids_.find(price);
        if (it == bids_.end()) return 0;
        uint64_t total = 0;
        for (const Order* o : it->second) {
            if (!o->cancelled) total += o->quantity;
        }
        return total;
    }

    uint64_t ask_qty_at(uint64_t price) const {
        auto it = asks_.find(price);
        if (it == asks_.end()) return 0;
        uint64_t total = 0;
        for (const Order* o : it->second) {
            if (!o->cancelled) total += o->quantity;
        }
        return total;
    }

    uint64_t total_trades()  const { return trade_count_; }
    uint64_t total_orders()  const { return order_count_; }
    uint64_t total_cancels() const { return cancel_count_; }

private:
    // Red-black tree: every lookup is O(log n) with guaranteed pointer chasing
    // Each tree node is a separate heap allocation — cache-hostile
    std::map<uint64_t, std::deque<Order*>> bids_;  // highest price = best bid
    std::map<uint64_t, std::deque<Order*>> asks_;  // lowest price  = best ask

    // Hash map for cancel lookup (same idea as optimized, but Order* is heap-scattered)
    std::unordered_map<uint64_t, Order*> id_to_order_;

    uint64_t trade_count_  = 0;
    uint64_t order_count_  = 0;
    uint64_t cancel_count_ = 0;

    // ── Matching ─────────────────────────────────────────────────────────────

    void match_buy(Order* incoming) {
        // Sweep asks from lowest price upward
        while (incoming->quantity > 0 && !asks_.empty()) {
            auto ask_it = asks_.begin();  // lowest ask — O(log n) in std::map
            if (ask_it->first > incoming->price) break;

            auto& queue = ask_it->second;
            clean_front(queue);

            if (queue.empty()) {
                asks_.erase(ask_it);
                continue;
            }

            Order* resting = queue.front();
            uint64_t fill = std::min(incoming->quantity, resting->quantity);

            emit_trade(incoming, resting, fill);
            incoming->quantity -= fill;
            resting->quantity  -= fill;

            if (resting->quantity == 0) {
                id_to_order_.erase(resting->order_id);
                queue.pop_front();
                delete resting;    // free back to heap — the slow part
                if (queue.empty()) asks_.erase(ask_it);
            }
        }

        if (incoming->quantity > 0 && incoming->type == OrderType::LIMIT) {
            rest_order_bid(incoming);
        } else {
            delete incoming;
        }
    }

    void match_sell(Order* incoming) {
        // Sweep bids from highest price downward
        while (incoming->quantity > 0 && !bids_.empty()) {
            auto bid_it = std::prev(bids_.end());  // highest bid — O(log n)
            if (bid_it->first < incoming->price) break;

            auto& queue = bid_it->second;
            clean_front(queue);

            if (queue.empty()) {
                bids_.erase(bid_it);
                continue;
            }

            Order* resting = queue.front();
            uint64_t fill = std::min(incoming->quantity, resting->quantity);

            emit_trade(resting, incoming, fill);
            incoming->quantity -= fill;
            resting->quantity  -= fill;

            if (resting->quantity == 0) {
                id_to_order_.erase(resting->order_id);
                queue.pop_front();
                delete resting;
                if (queue.empty()) bids_.erase(bid_it);
            }
        }

        if (incoming->quantity > 0 && incoming->type == OrderType::LIMIT) {
            rest_order_ask(incoming);
        } else {
            delete incoming;
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    void rest_order_bid(Order* o) {
        id_to_order_[o->order_id] = o;
        bids_[o->price].push_back(o);
    }

    void rest_order_ask(Order* o) {
        id_to_order_[o->order_id] = o;
        asks_[o->price].push_back(o);
    }

    void clean_front(std::deque<Order*>& queue) {
        while (!queue.empty()) {
            Order* front = queue.front();
            if (!front->cancelled && front->quantity > 0) return;
            id_to_order_.erase(front->order_id);
            queue.pop_front();
            delete front;
        }
    }

    void emit_trade(Order* buy, Order* sell, uint64_t qty) {
        ++trade_count_;
        if (!on_trade) return;
        Trade t;
        t.buy_order_id  = buy->order_id;
        t.sell_order_id = sell->order_id;
        t.price         = sell->price;
        t.quantity      = qty;
        t.timestamp     = now_ns();
        on_trade(t);
    }

    static inline uint64_t now_ns() {
        // Deliberately using chrono instead of rdtsc — extra overhead
        auto now = std::chrono::high_resolution_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count());
    }
};

// ─── Replay adapter (same signature as optimized version) ────────────────────
// This overload lets bench_standalone.cpp call replay(naive_book, orders)
// with the same code path.

#include "csv_parser.hpp"

inline void replay(NaiveOrderBook& book, const std::vector<OrderMsg>& msgs) {
    for (const auto& m : msgs) {
        switch (m.type) {
            case OrderType::LIMIT:
                book.add_limit_order(m.order_id, m.side, m.price, m.quantity);
                break;
            case OrderType::MARKET:
                book.add_market_order(m.order_id, m.side, m.quantity);
                break;
            case OrderType::CANCEL:
                book.cancel_order(m.price);  // price field holds cancel target id
                break;
        }
    }
}
