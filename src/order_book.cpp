#include "order_book.hpp"
#include <algorithm>
#include <cstring>

// Portable branch prediction hint
#if defined(_MSC_VER)
#define UNLIKELY(x) (x)
#else
#define UNLIKELY(x) __builtin_expect((x), 0)
#endif

OrderBook::OrderBook() {
    bids_ = new PriceLevel[LEVELS]();
    asks_ = new PriceLevel[LEVELS]();
    for (uint64_t i = 0; i < LEVELS; ++i) {
        bids_[i].price = MIN_PRICE + i;
        asks_[i].price = MIN_PRICE + i;
    }
    id_to_idx_.reserve(100'000);
}

OrderBook::~OrderBook() {
    delete[] bids_;
    delete[] asks_;
}

// ─── Public API ──────────────────────────────────────────────────────────────

void OrderBook::add_limit_order(uint64_t id, Side side,
                                 uint64_t price, uint64_t qty) {
    if (UNLIKELY(price < MIN_PRICE || price > MAX_PRICE)) return;
    ++order_count_;
    Order* o    = pool_.alloc();
    o->order_id = id;
    o->price    = price;
    o->quantity = qty;
    o->side     = side;
    o->type     = OrderType::LIMIT;
    o->timestamp = now_tsc();
    if (side == Side::BUY) match_buy(o);
    else                    match_sell(o);
}

void OrderBook::add_market_order(uint64_t id, Side side, uint64_t qty) {
    ++order_count_;
    Order* o    = pool_.alloc();
    o->order_id = id;
    o->quantity = qty;
    o->side     = side;
    o->type     = OrderType::MARKET;
    o->timestamp = now_tsc();
    if (side == Side::BUY) { o->price = UINT64_MAX; match_buy(o);  }
    else                   { o->price = 0;           match_sell(o); }
}

void OrderBook::cancel_order(uint64_t id) {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end()) return;
    Order* o = pool_.get(it->second);
    o->cancelled = true;
    if (o->side == Side::BUY) bid_level(o->price).reduce_qty(o->quantity);
    else                       ask_level(o->price).reduce_qty(o->quantity);
    id_to_idx_.erase(it);
    ++cancel_count_;
}

// ─── Stale-order cleanup helper ───────────────────────────────────────────────
// Pops and frees any cancelled or zero-qty orders at the front of a level.
// Returns true if the level still has a valid order after cleanup.
static bool clean_front(PriceLevel& level, EnginePool& pool,
                         std::unordered_map<uint64_t,uint32_t>& idx_map) {
    while (!level.empty()) {
        Order* front = level.front();
        if (!front->cancelled && front->quantity > 0) return true;
        // Stale — remove
        idx_map.erase(front->order_id);
        level.pop();
        pool.dealloc(front);
    }
    return false;
}

// ─── Matching ────────────────────────────────────────────────────────────────

void OrderBook::match_buy(Order* incoming) {
    while (incoming->quantity > 0
           && best_ask_ != UINT64_MAX
           && best_ask_ <= incoming->price)
    {
        PriceLevel& level = ask_level(best_ask_);

        if (!clean_front(level, pool_, id_to_idx_)) {
            active_asks_.erase(best_ask_);
            update_best_ask();
            continue;
        }

        Order* resting = level.front();
        uint64_t fill  = std::min(incoming->quantity, resting->quantity);

        emit_trade(incoming, resting, fill);
        ask_level(best_ask_).fill_qty(fill);
        incoming->quantity -= fill;
        resting->quantity  -= fill;

        if (resting->quantity == 0) {
            id_to_idx_.erase(resting->order_id);
            level.pop();
            pool_.dealloc(resting);
            if (level.empty()) {
                active_asks_.erase(best_ask_);
                update_best_ask();
            }
        }
    }

    // Only LIMIT orders rest; market orders are discarded in add_market_order
    if (incoming->quantity > 0 && incoming->type == OrderType::LIMIT) {
        rest_order(incoming);
        if (incoming->price > best_bid_) best_bid_ = incoming->price;
    } else {
        pool_.dealloc(incoming);
    }
}

void OrderBook::match_sell(Order* incoming) {
    while (incoming->quantity > 0
           && best_bid_ != 0
           && best_bid_ >= incoming->price)
    {
        PriceLevel& level = bid_level(best_bid_);

        if (!clean_front(level, pool_, id_to_idx_)) {
            active_bids_.erase(best_bid_);
            update_best_bid();
            continue;
        }

        Order* resting = level.front();
        uint64_t fill  = std::min(incoming->quantity, resting->quantity);

        emit_trade(resting, incoming, fill);
        bid_level(best_bid_).fill_qty(fill);
        incoming->quantity -= fill;
        resting->quantity  -= fill;

        if (resting->quantity == 0) {
            id_to_idx_.erase(resting->order_id);
            level.pop();
            pool_.dealloc(resting);
            if (level.empty()) {
                active_bids_.erase(best_bid_);
                update_best_bid();
            }
        }
    }

    if (incoming->quantity > 0 && incoming->type == OrderType::LIMIT) {
        rest_order(incoming);
        if (incoming->price < best_ask_) best_ask_ = incoming->price;
    } else {
        pool_.dealloc(incoming);
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void OrderBook::rest_order(Order* o) {
    id_to_idx_[o->order_id] = pool_.index_of(o);
    if (o->side == Side::BUY) {
        bid_level(o->price).push(o);
        active_bids_.insert(o->price);
    } else {
        ask_level(o->price).push(o);
        active_asks_.insert(o->price);
    }
}

void OrderBook::update_best_bid() {
    while (!active_bids_.empty()) {
        auto it = active_bids_.end(); --it;
        uint64_t p = *it;
        PriceLevel& lv = bid_level(p);
        // Clean stale orders before deciding if level is active
        if (clean_front(lv, pool_, id_to_idx_)) { best_bid_ = p; return; }
        active_bids_.erase(it);
    }
    best_bid_ = 0;
}

void OrderBook::update_best_ask() {
    while (!active_asks_.empty()) {
        auto it = active_asks_.begin();
        uint64_t p = *it;
        PriceLevel& lv = ask_level(p);
        if (clean_front(lv, pool_, id_to_idx_)) { best_ask_ = p; return; }
        active_asks_.erase(it);
    }
    best_ask_ = UINT64_MAX;
}

void OrderBook::emit_trade(Order* buy, Order* sell, uint64_t qty) {
    ++trade_count_;
    if (!on_trade) return;
    Trade t;
    t.buy_order_id  = buy->order_id;
    t.sell_order_id = sell->order_id;
    t.price         = sell->price;
    t.quantity      = qty;
    t.timestamp     = now_tsc();
    on_trade(t);
}

uint64_t OrderBook::bid_qty_at(uint64_t price) const {
    if (price < MIN_PRICE || price > MAX_PRICE) return 0;
    return bids_[price - MIN_PRICE].total_qty;
}

uint64_t OrderBook::ask_qty_at(uint64_t price) const {
    if (price < MIN_PRICE || price > MAX_PRICE) return 0;
    return asks_[price - MIN_PRICE].total_qty;
}

std::vector<LevelInfo> OrderBook::bid_levels(int n) const {
    std::vector<LevelInfo> result;
    result.reserve(n);
    for (auto it = active_bids_.rbegin(); it != active_bids_.rend() && (int)result.size() < n; ++it) {
        const PriceLevel& lvl = bids_[*it - MIN_PRICE];
        if (!lvl.empty())
            result.push_back({lvl.price, lvl.total_qty, lvl.order_count});
    }
    return result;
}

std::vector<LevelInfo> OrderBook::ask_levels(int n) const {
    std::vector<LevelInfo> result;
    result.reserve(n);
    for (auto it = active_asks_.begin(); it != active_asks_.end() && (int)result.size() < n; ++it) {
        const PriceLevel& lvl = asks_[*it - MIN_PRICE];
        if (!lvl.empty())
            result.push_back({lvl.price, lvl.total_qty, lvl.order_count});
    }
    return result;
}
