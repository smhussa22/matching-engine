#ifndef ORDER_BOOK_HH
#define ORDER_BOOK_HH

#include "OrderPool.hh"
#include "Order.hh"

// class representing a limit order book
// an orderbook is a data structure; a list of buy and sell orders for an asset
// these buy and sell orders are oganized by a price
// there are bids (prices buyers are willing to pay) and asks (prices sellers are willing to take)
// the difference between the highest bid and lowest ask is the current "spread" of the orderbook
// the spread is important because it indicates liquidity, i.e., the cost of immediately buying or selling
// a limit order book is just an orderbook with a specific clause:
// buy and sell orders specify the maximum price a buyer will pay or the minimum price a seller will accept

class OrderBook
{
public:
    OrderBook() = default;
    ~OrderBook() = default;

    // an orderbook holds all of the state; millions of orders, price levels, resting orders, the order pool
    // for this reason we should not make it copyable
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // for the same reasons as not making it copyable, do not make it moveable
    // technically this could be handled, but it is not a priority.
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

private:
    OrderPool order_pool;

};

#endif // ORDER_BOOK_HH