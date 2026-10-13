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

    // all a "user" of this order book should do is be able to submit or cancel an order
    // how we do that for them depending on the order should remain hidden from them

    // the only way a new order enters the book. this is the "front door":
    // it checks the order is valid (quantity not zero, price in range, id not
    // already used etc), tries to match it against waiting orders, and if any
    // quantity is left over, rests it in the book.
    // a bad order is rejected here and never touches the book.
    void submit_order();
    
    // removes a resting order by its id
    // looks the order up, unlinks it from its queue, frees its pool slot, and
    // rejects with not found if the id doesnt exist or already filled.
    void cancel_order();

private:
    OrderPool m_order_pool;

    // a "level" is all the resting orders at one price on one direction of the book.
    // e.g. buying at 10 is one level. they are organized into a queue of orders, for each level
    // (linked with prev/next) plus the total quantity waiting at that price.
    // the two directions each hold many levels, one per price.
    struct Level
    {

        int32_t m_head { -1 }; // front of queue (what to match first)
        int32_t m_tail { -1 }; // back of queue

        // total quantity of every order waiting at this level
        // simply a running total.
        uint32_t m_total_volume { 0 }; 

    };

    // puts an order into the book to wait. it only does the storing: grabs a
    // pool slot, links the order onto the back of its price level's queue, and
    // updates the level's total volume. it does no matching.
    // submit vs add: submit is the whole process (validate, match, then rest
    // the leftover). add is just the last step, resting the leftover.
    void add_order();

    // matches an incoming order against the opposite side of the book.
    // a buy looks at the lowest ask, a sell looks at the highest bid. while
    // the order still has quantity and its price is good enough to cross, it
    // works through the best price level, then the next best, and so on.
    // match_order decides *which level* to trade against next.
    void match_order();

    // trades an incoming order against one level, one price.
    // takes orders from the front of the queue (oldest first, which is
    // price-time priority), trades min(incoming, resting) each time, and
    // removes resting orders that fully fill. stops when the incoming order is
    // done or the level is empty.
    // match_order walks the levels, match_with_level works inside one level.
    void match_with_level();

};

#endif // ORDER_BOOK_HH