#include <gtest/gtest.h>
#include <memory>
#include "OrderBook.hh"

// captures every callback so tests can make assertions on them
struct RecordingListener
{
    struct Trade
    {
        uint64_t aggressor_id;
        uint64_t passive_id;
        int32_t  price;
        uint32_t qty;
    };

    std::vector<Trade>                              trades;
    std::vector<uint64_t>                           added_ids;
    std::vector<uint64_t>                           cancelled_ids;
    std::vector<std::pair<uint64_t, OrderRejected>> rejections;

    void on_trade(uint64_t agg, uint64_t pass, int32_t price, uint32_t qty)
    {
        trades.push_back({agg, pass, price, qty});
    }

    void on_order_added(uint64_t id, int32_t, uint32_t, OrderDirection)
    {
        added_ids.push_back(id);
    }

    void on_order_cancelled(uint64_t id)
    {
        cancelled_ids.push_back(id);
    }

    void on_order_rejected(uint64_t id, OrderRejected reason)
    {
        rejections.push_back({id, reason});
    }

    void on_order_book_update(int32_t, uint32_t, OrderDirection) {}
};

class OrderBookTest : public ::testing::Test
{
protected:
    RecordingListener listener;
    std::unique_ptr<OrderBook<RecordingListener>> book;

    void SetUp() override
    {
        book = std::make_unique<OrderBook<RecordingListener>>(listener);
    }
};

// ---- rejections ----------------------------------------------------------------

TEST_F(OrderBookTest, RejectsZeroQuantity)
{
    book->submit_order(Order(1, 100, 0, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidQuantity);
}

TEST_F(OrderBookTest, RejectsZeroPrice)
{
    book->submit_order(Order(1, 0, 10, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidPrice);
}

TEST_F(OrderBookTest, RejectsNegativePrice)
{
    book->submit_order(Order(1, -1, 10, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidPrice);
}

TEST_F(OrderBookTest, RejectsDuplicateId)
{
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(1, 101, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::DuplicateId);
}

// ---- GTC matching --------------------------------------------------------------

TEST_F(OrderBookTest, GtcFullMatch)
{
    // sell rests, buy arrives and fills it completely
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].aggressor_id, 2u);
    EXPECT_EQ(listener.trades[0].passive_id,   1u);
    EXPECT_EQ(listener.trades[0].price,        100);
    EXPECT_EQ(listener.trades[0].qty,          10u);
    EXPECT_TRUE(listener.rejections.empty());
}

TEST_F(OrderBookTest, GtcIncomingFilledCompletely)
{
    // sell 10, buy 5 — buy fills completely and does not rest, sell keeps 5 waiting
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 5,  OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);

    // only the sell should have rested; the buy filled and never rested
    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, GtcRestingFilledPartially)
{
    // sell 5, buy 10 — sell fills completely, buy rests with 5 remaining
    book->submit_order(Order(1, 100, 5,  OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);

    // buy had leftover so it rested too
    ASSERT_EQ(listener.added_ids.size(), 2u);
    EXPECT_EQ(listener.added_ids[1], 2u);
}

TEST_F(OrderBookTest, GtcMultiLevelSweep)
{
    // buy sweeps through two ask levels and rests the leftover
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 101, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(3, 102, 25, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 2u);
    EXPECT_EQ(listener.trades[0].price, 100);
    EXPECT_EQ(listener.trades[0].qty,   10u);
    EXPECT_EQ(listener.trades[1].price, 101);
    EXPECT_EQ(listener.trades[1].qty,   10u);

    // 5 leftover from the buy rests at 102
    ASSERT_EQ(listener.added_ids.size(), 3u);
    EXPECT_EQ(listener.added_ids[2], 3u);
}

TEST_F(OrderBookTest, GtcTimePriority)
{
    // two sells at the same price — the oldest one should fill first
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(3, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].passive_id, 1u);
}

// ---- cancellation --------------------------------------------------------------

TEST_F(OrderBookTest, CancelRestingOrder)
{
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->cancel_order(1);

    ASSERT_EQ(listener.cancelled_ids.size(), 1u);
    EXPECT_EQ(listener.cancelled_ids[0], 1u);
    EXPECT_TRUE(listener.rejections.empty());
}

TEST_F(OrderBookTest, CancelUnknownId)
{
    book->cancel_order(999);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  999u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::NotFound);
}

TEST_F(OrderBookTest, CancelAlreadyFilledOrder)
{
    // fill the order completely then try to cancel — it's already gone from the book
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);
    book->cancel_order(1);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::NotFound);
}

// ---- FAK -----------------------------------------------------------------------

TEST_F(OrderBookTest, FakFullFill)
{
    // FAK fills completely — behaves the same as GTC when it fully fills
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::FAK);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 10u);

    // FAK order must never rest, even when fully filled
    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, FakPartialFillKillsRemainder)
{
    // sell 5, FAK buy 10 — trades 5, the remaining 5 is discarded
    book->submit_order(Order(1, 100, 5,  OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::FAK);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);

    // FAK remainder must not rest
    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, FakNoMatchDiscarded)
{
    // prices don't cross — FAK gets nothing and is silently discarded
    book->submit_order(Order(1, 101, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 99,  10, OrderDirection::Buy),  OrderType::FAK);

    EXPECT_TRUE(listener.trades.empty());
    EXPECT_TRUE(listener.rejections.empty());

    // only the sell rested; FAK buy must not appear in the book
    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}
