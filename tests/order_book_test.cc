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
        bool     aggressor_filled;
        bool     passive_filled;
    };

    std::vector<Trade>                              trades;
    std::vector<uint64_t>                           added_ids;
    std::vector<uint64_t>                           cancelled_ids;
    std::vector<std::pair<uint64_t, OrderRejected>> rejections;

    void on_trade(uint64_t agg, uint64_t pass, int32_t price, uint32_t qty, bool agg_filled, bool pass_filled)
    {
        trades.push_back({agg, pass, price, qty, agg_filled, pass_filled});
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

TEST_F(OrderBookTest, RejectsZeroQuantity)
{
    // orders with zero quantity are refused with invalidquantity
    book->submit_order(Order(1, 100, 0, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidQuantity);
}

TEST_F(OrderBookTest, RejectsZeroPrice)
{
    // zero price is outside the valid range and is refused
    book->submit_order(Order(1, 0, 10, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidPrice);
}

TEST_F(OrderBookTest, RejectsNegativePrice)
{
    // negative price is outside the valid range and is refused
    book->submit_order(Order(1, -1, 10, OrderDirection::Buy), OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::InvalidPrice);
}

TEST_F(OrderBookTest, RejectsDuplicateId)
{
    // two orders with the same id — the second one is refused as a duplicate
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(1, 101, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::DuplicateId);
}

TEST_F(OrderBookTest, GtcFullMatch)
{
    // sell rests, buy arrives and fills it completely so both sides are reported filled
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].aggressor_id,     2u);
    EXPECT_EQ(listener.trades[0].passive_id,       1u);
    EXPECT_EQ(listener.trades[0].price,            100);
    EXPECT_EQ(listener.trades[0].qty,              10u);
    EXPECT_TRUE(listener.trades[0].aggressor_filled);
    EXPECT_TRUE(listener.trades[0].passive_filled);
    EXPECT_TRUE(listener.rejections.empty());
}

TEST_F(OrderBookTest, GtcIncomingFilledCompletely)
{
    // sell 10, buy 5 — aggressor fully fills but passive keeps 5 waiting in the book
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 5,  OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);
    EXPECT_TRUE (listener.trades[0].aggressor_filled);
    EXPECT_FALSE(listener.trades[0].passive_filled);

    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, GtcRestingFilledPartially)
{
    // sell 5, buy 10 — passive fully fills, aggressor has 5 left and rests in the book
    book->submit_order(Order(1, 100, 5,  OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);
    EXPECT_FALSE(listener.trades[0].aggressor_filled);
    EXPECT_TRUE (listener.trades[0].passive_filled);

    ASSERT_EQ(listener.added_ids.size(), 2u);
    EXPECT_EQ(listener.added_ids[1], 2u);
}

TEST_F(OrderBookTest, GtcMultiLevelSweep)
{
    // buy walks through two ask levels and rests the leftover at its own price
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 101, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(3, 102, 25, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 2u);
    EXPECT_EQ(listener.trades[0].price, 100);
    EXPECT_EQ(listener.trades[0].qty,   10u);
    EXPECT_EQ(listener.trades[1].price, 101);
    EXPECT_EQ(listener.trades[1].qty,   10u);

    ASSERT_EQ(listener.added_ids.size(), 3u);
    EXPECT_EQ(listener.added_ids[2], 3u);
}

TEST_F(OrderBookTest, GtcTimePriority)
{
    // two sells at the same price — the oldest one fills first (fifo within a level)
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(3, 100, 10, OrderDirection::Buy),  OrderType::GTC);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].passive_id, 1u);
}

TEST_F(OrderBookTest, CancelRestingOrder)
{
    // a resting order is removed on cancel and the client gets a cancel confirmation
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->cancel_order(1);

    ASSERT_EQ(listener.cancelled_ids.size(), 1u);
    EXPECT_EQ(listener.cancelled_ids[0], 1u);
    EXPECT_TRUE(listener.rejections.empty());
}

TEST_F(OrderBookTest, CancelUnknownId)
{
    // cancelling an id that was never in the book is refused with notfound
    book->cancel_order(999);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].first,  999u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::NotFound);
}

TEST_F(OrderBookTest, CancelAlreadyFilledOrder)
{
    // after an order fully fills it is no longer in the book so cancel returns notfound
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::GTC);
    book->cancel_order(1);

    ASSERT_EQ(listener.rejections.size(), 1u);
    EXPECT_EQ(listener.rejections[0].second, OrderRejected::NotFound);
}

TEST_F(OrderBookTest, FakFullFill)
{
    // fak order fills completely and never rests even though quantity matches exactly
    book->submit_order(Order(1, 100, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::FAK);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 10u);

    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, FakPartialFillKillsRemainder)
{
    // fak buy for 10 but only 5 available — fills 5 and discards the rest without resting
    book->submit_order(Order(1, 100, 5,  OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 100, 10, OrderDirection::Buy),  OrderType::FAK);

    ASSERT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].qty, 5u);

    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}

TEST_F(OrderBookTest, FakNoMatchDiscarded)
{
    // prices do not cross so the fak buy gets no fills and is silently thrown away
    book->submit_order(Order(1, 101, 10, OrderDirection::Sell), OrderType::GTC);
    book->submit_order(Order(2, 99,  10, OrderDirection::Buy),  OrderType::FAK);

    EXPECT_TRUE(listener.trades.empty());
    EXPECT_TRUE(listener.rejections.empty());

    ASSERT_EQ(listener.added_ids.size(), 1u);
    EXPECT_EQ(listener.added_ids[0], 1u);
}
