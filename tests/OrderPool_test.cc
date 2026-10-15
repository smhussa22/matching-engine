#include <gtest/gtest.h>
#include "OrderPool.hh"

TEST(OrderPoolTest, AllocateReturnsValidIndex)
{
    // allocating from a non-empty pool returns a non-negative index
    OrderPool pool { 10 };
    int32_t idx = pool.allocate(1u, 100, 10, OrderDirection::Buy);
    EXPECT_GE(idx, 0);
}

TEST(OrderPoolTest, AllocateConstructsOrderWithGivenFields)
{
    // the order at the returned index carries the values passed to allocate
    OrderPool pool { 10 };
    int32_t idx = pool.allocate(42u, 500, 25, OrderDirection::Sell);
    const Order& o = pool.get(idx);

    EXPECT_EQ(o.m_id,        42u);
    EXPECT_EQ(o.m_price,     500);
    EXPECT_EQ(o.m_quantity,  25u);
    EXPECT_EQ(o.m_direction, OrderDirection::Sell);
}

TEST(OrderPoolTest, AllocatedOrderHasPrevNextMinusOne)
{
    // allocated orders start unlinked, with prev and next set to -1 so they can be linked into a level cleanly
    OrderPool pool { 10 };
    int32_t idx = pool.allocate(1u, 100, 10, OrderDirection::Buy);
    const Order& o = pool.get(idx);

    EXPECT_EQ(o.m_prev, -1);
    EXPECT_EQ(o.m_next, -1);
}

TEST(OrderPoolTest, AllocateReturnsMinusOneWhenFull)
{
    // once every slot has been handed out, allocate returns -1 to signal exhaustion
    OrderPool pool { 3 };
    EXPECT_GE(pool.allocate(1u, 100, 10, OrderDirection::Buy), 0);
    EXPECT_GE(pool.allocate(2u, 100, 10, OrderDirection::Buy), 0);
    EXPECT_GE(pool.allocate(3u, 100, 10, OrderDirection::Buy), 0);
    EXPECT_EQ(pool.allocate(4u, 100, 10, OrderDirection::Buy), -1);
}

TEST(OrderPoolTest, DeallocateMakesSlotAvailableAgain)
{
    // freeing a slot returns it to the free list so a later allocate can reuse it
    OrderPool pool { 1 };
    int32_t idx = pool.allocate(1u, 100, 10, OrderDirection::Buy);
    ASSERT_GE(idx, 0);
    EXPECT_EQ(pool.allocate(2u, 100, 10, OrderDirection::Buy), -1);

    pool.deallocate(idx);
    EXPECT_GE(pool.allocate(3u, 100, 10, OrderDirection::Buy), 0);
}
