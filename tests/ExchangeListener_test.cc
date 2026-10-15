#include <gtest/gtest.h>
#include <maroof/net/Connection.hh>
#include <maroof/net/Endpoint.hh>
#include "ExchangeListener.hh"

class ExchangeListenerTest : public ::testing::Test
{
protected:
    maroof::net::Endpoint   endpoint1 { "127.0.0.1", 9001 };
    maroof::net::Endpoint   endpoint2 { "127.0.0.1", 9002 };
    maroof::net::Connection conn1     { endpoint1 };
    maroof::net::Connection conn2     { endpoint2 };
    ExchangeListener        listener;
};

TEST_F(ExchangeListenerTest, RegisterConnectionAddsToClientSet)
{
    // registering a connection makes it visible in the broadcast set
    listener.register_connection(&conn1);

    EXPECT_EQ(listener.client_count(), 1u);
    EXPECT_TRUE(listener.contains_client(&conn1));
}

TEST_F(ExchangeListenerTest, UnregisterConnectionRemovesFromClientSet)
{
    // after unregister, the connection is gone from the broadcast set
    listener.register_connection(&conn1);
    listener.unregister_connection(&conn1);

    EXPECT_EQ(listener.client_count(), 0u);
    EXPECT_FALSE(listener.contains_client(&conn1));
}

TEST_F(ExchangeListenerTest, RegisterOrderAddsToOrderMap)
{
    // registering an order lets the listener route private messages back to the right client
    listener.register_order(42, &conn1);

    EXPECT_EQ(listener.order_count(), 1u);
    EXPECT_TRUE(listener.contains_order(42));
}

TEST_F(ExchangeListenerTest, UnregisterConnectionCleansUpOrders)
{
    // if a client disconnects while holding orders, those orders are purged to avoid dangling pointers
    listener.register_connection(&conn1);
    listener.register_order(1, &conn1);
    listener.register_order(2, &conn1);
    listener.register_order(3, &conn2);

    listener.unregister_connection(&conn1);

    EXPECT_FALSE(listener.contains_order(1));
    EXPECT_FALSE(listener.contains_order(2));
    EXPECT_TRUE (listener.contains_order(3));
}

TEST_F(ExchangeListenerTest, OrderCancelledErasesEntry)
{
    // a cancelled order is terminal so its map entry is dropped after the client is notified
    listener.register_order(1, &conn1);
    listener.on_order_cancelled(1);

    EXPECT_FALSE(listener.contains_order(1));
}

TEST_F(ExchangeListenerTest, OrderRejectedErasesEntry)
{
    // a rejected order never rested so its map entry is dropped after the client is notified
    listener.register_order(1, &conn1);
    listener.on_order_rejected(1, OrderRejected::DuplicateId);

    EXPECT_FALSE(listener.contains_order(1));
}

TEST_F(ExchangeListenerTest, TradeAggressorFilledErasesAggressorOnly)
{
    // aggressor fully fills but passive still rests, so only the aggressor id is removed
    listener.register_order(1, &conn1); // passive, resting
    listener.register_order(2, &conn2); // aggressor
    listener.on_trade(2, 1, 100, 5, true, false);

    EXPECT_FALSE(listener.contains_order(2));
    EXPECT_TRUE (listener.contains_order(1));
}

TEST_F(ExchangeListenerTest, TradePassiveFilledErasesPassiveOnly)
{
    // passive fully fills but aggressor still has leftover, so only the passive id is removed
    listener.register_order(1, &conn1); // passive
    listener.register_order(2, &conn2); // aggressor, will rest leftover
    listener.on_trade(2, 1, 100, 5, false, true);

    EXPECT_FALSE(listener.contains_order(1));
    EXPECT_TRUE (listener.contains_order(2));
}

TEST_F(ExchangeListenerTest, TradeBothFilledErasesBoth)
{
    // perfect fill where both sides are done, so both ids are removed from the map
    listener.register_order(1, &conn1);
    listener.register_order(2, &conn2);
    listener.on_trade(2, 1, 100, 10, true, true);

    EXPECT_FALSE(listener.contains_order(1));
    EXPECT_FALSE(listener.contains_order(2));
}
