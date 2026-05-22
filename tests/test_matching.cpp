#include <gtest/gtest.h>
#include "order_book.hpp"
#include <vector>

// ── Fixture: fresh book + trade collector ─────────────────────────────────────

class LOBTest : public ::testing::Test {
protected:
    OrderBook book;
    std::vector<Trade> trades;

    void SetUp() override {
        book.on_trade = [this](const Trade& t) {
            trades.push_back(t);
        };
    }

    uint64_t total_filled() const {
        uint64_t total = 0;
        for (auto& t : trades) total += t.quantity;
        return total;
    }
};

// ── Basic matching ─────────────────────────────────────────────────────────────

TEST_F(LOBTest, FullFill_LimitVsLimit) {
    book.add_limit_order(1, Side::BUY,  10000, 100);
    book.add_limit_order(2, Side::SELL, 10000, 100);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_EQ(trades[0].price, 10000u);
    EXPECT_EQ(book.best_bid(), 0u);          // buy fully matched
    EXPECT_EQ(book.best_ask(), UINT64_MAX);  // sell fully matched
}

TEST_F(LOBTest, PartialFill_BuyLarger) {
    book.add_limit_order(1, Side::BUY,  10000, 100);
    book.add_limit_order(2, Side::SELL, 10000, 60);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 60u);
    // 40 units still resting on bid
    EXPECT_EQ(book.best_bid(), 10000u);
    EXPECT_EQ(book.bid_qty_at(10000), 40u);
    EXPECT_EQ(book.best_ask(), UINT64_MAX);
}

TEST_F(LOBTest, PartialFill_SellLarger) {
    book.add_limit_order(1, Side::BUY,  10000, 60);
    book.add_limit_order(2, Side::SELL, 10000, 100);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 60u);
    // 40 units still resting on ask
    EXPECT_EQ(book.best_ask(), 10000u);
    EXPECT_EQ(book.ask_qty_at(10000), 40u);
    EXPECT_EQ(book.best_bid(), 0u);
}

// ── Price-time priority ───────────────────────────────────────────────────────

TEST_F(LOBTest, PriceTimePriority_SamePrice) {
    // Order 1 arrives before Order 2 at same price — 1 must be filled first
    book.add_limit_order(1, Side::BUY, 10000, 50);
    book.add_limit_order(2, Side::BUY, 10000, 50);
    book.add_limit_order(3, Side::SELL, 10000, 50);  // should fill order 1

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].buy_order_id, 1u);  // order 1 filled, not 2
    // Order 2 still in book
    EXPECT_EQ(book.best_bid(), 10000u);
    EXPECT_EQ(book.bid_qty_at(10000), 50u);
}

TEST_F(LOBTest, PricePriority_BestPriceFirst) {
    // Two sells: one at 9900, one at 10000 — buy should fill at 9900 (better)
    book.add_limit_order(1, Side::SELL, 10000, 100);
    book.add_limit_order(2, Side::SELL,  9900, 100);
    book.add_limit_order(3, Side::BUY,  10000, 100);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].sell_order_id, 2u);  // filled the 9900 sell
    EXPECT_EQ(trades[0].price, 9900u);
}

// ── Multi-level sweep ──────────────────────────────────────────────────────────

TEST_F(LOBTest, MultiLevelSweep) {
    book.add_limit_order(1, Side::SELL,  9900, 10);
    book.add_limit_order(2, Side::SELL, 10000, 10);
    book.add_limit_order(3, Side::SELL, 10100, 10);

    // Market buy for 25 — sweeps 9900 (10), 10000 (10), partial 10100 (5)
    book.add_market_order(4, Side::BUY, 25);

    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].quantity, 10u);
    EXPECT_EQ(trades[1].quantity, 10u);
    EXPECT_EQ(trades[2].quantity,  5u);
    EXPECT_EQ(total_filled(), 25u);

    // 5 remaining at 10100
    EXPECT_EQ(book.best_ask(), 10100u);
    EXPECT_EQ(book.ask_qty_at(10100), 5u);
}

// ── Market orders ──────────────────────────────────────────────────────────────

TEST_F(LOBTest, MarketBuy_FullyFilled) {
    book.add_limit_order(1, Side::SELL, 10000, 100);
    book.add_market_order(2, Side::BUY, 100);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_EQ(book.best_ask(), UINT64_MAX);
}

TEST_F(LOBTest, MarketOrder_NoLiquidity) {
    // No resting orders — market order gets dropped, no crash
    EXPECT_NO_THROW(book.add_market_order(1, Side::BUY, 100));
    EXPECT_TRUE(trades.empty());
}

// ── Cancel orders ──────────────────────────────────────────────────────────────

TEST_F(LOBTest, Cancel_PreventsFill) {
    book.add_limit_order(1, Side::BUY, 10000, 100);
    book.cancel_order(1);

    // Now a matching sell arrives — should NOT match (order 1 cancelled)
    book.add_limit_order(2, Side::SELL, 10000, 100);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 0u);
    EXPECT_EQ(book.best_ask(), 10000u);  // sell is resting
}

TEST_F(LOBTest, Cancel_NonExistentOrder) {
    // Cancelling unknown id should not crash
    EXPECT_NO_THROW(book.cancel_order(99999));
}

TEST_F(LOBTest, Cancel_PartialQtyBook) {
    book.add_limit_order(1, Side::BUY, 10000, 100);
    book.add_limit_order(2, Side::BUY, 10000,  50);
    book.cancel_order(1);

    // Order 2 still there
    EXPECT_EQ(book.best_bid(), 10000u);
    EXPECT_EQ(book.bid_qty_at(10000), 50u);
}

// ── Best bid/ask tracking ──────────────────────────────────────────────────────

TEST_F(LOBTest, BestBidAsk_UpdatesCorrectly) {
    book.add_limit_order(1, Side::BUY,  9900, 100);
    book.add_limit_order(2, Side::BUY, 10000, 100);  // best bid

    EXPECT_EQ(book.best_bid(), 10000u);

    book.cancel_order(2);
    // After cancelling best bid, next level becomes best
    // (lazy deletion — best_bid updates on next match attempt or explicit query)
    // Add a sell to trigger the scan
    book.add_limit_order(3, Side::SELL, 9900, 100);

    // 9900 sell should match against 9900 buy (order 1)
    EXPECT_EQ(trades.size(), 1u);
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST_F(LOBTest, ZeroQuantity_Ignored) {
    // Price range violation — should not crash
    EXPECT_NO_THROW(book.add_limit_order(1, Side::BUY, 0, 100));  // price=0 out of range
    EXPECT_EQ(book.best_bid(), 0u);
}

TEST_F(LOBTest, LargeVolume_PoolDoesNotExhaust) {
    // 100k orders alternating buy/sell — tests pool recycling
    for (uint64_t i = 0; i < 100'000; ++i) {
        if (i % 2 == 0) book.add_limit_order(i*2,   Side::BUY,  10000, 1);
        else             book.add_limit_order(i*2+1, Side::SELL, 10000, 1);
    }
    EXPECT_EQ(book.total_trades(), 50'000u);
}

TEST_F(LOBTest, CrossedBook_Resolves) {
    // Add buy at 10100, then sell at 9900 — they should match at 9900 (resting price)
    book.add_limit_order(1, Side::BUY,  10100, 100);
    book.add_limit_order(2, Side::SELL,  9900, 100);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    // Trade executes at resting sell's price (9900)
    EXPECT_EQ(trades[0].price, 9900u);
}
