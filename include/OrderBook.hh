#ifndef ORDER_BOOK_HH
#define ORDER_BOOK_HH

#include "OrderPool.hh"
#include "Order.hh"

#include <concepts>

// class representing a limit order book
// an orderbook is a data structure; a list of buy and sell orders for an asset
// these buy and sell orders are oganized by a price
// there are bids (prices buyers are willing to pay) and asks (prices sellers are willing to take)
// the difference between the highest bid and lowest ask is the current "spread" of the orderbook
// the spread is important because it indicates liquidity, i.e., the cost of immediately buying or selling
// a limit order book is just an orderbook with a specific clause:
// buy and sell orders specify the maximum price a buyer will pay or the minimum price a seller will accept

// a concept, i.e. the functions that anything listening to a trade needs to define
template <typename T>
concept TradeListener = requires(T t, uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction, OrderRejected reason)
{

    // called when two orders are matched
    // (incoming id, resting id, price, quantity)
    // carries both ids so both sides can be told via one of the concept functions 
    // e.g. t.on_trade(7, 3, 1000, 50) means incoming order 7 traded with resting order 3
    // for 50 units at price 1000
    t.on_trade(id, id, price, quantity);
    
    // called when an order is added as a resting order into the order book
    // (order id, price, quantity, direction)
    // e.g. t.on_order_added(7, 1000, 50, OrderDirection::Buy) means order 7 is now waiting
    // in the book to buy 50 units at price 1000
    t.on_order_added(id, price, quantity, direction);
    
    // called when a resting order is cancelled
    // (order id)
    // e.g. t.on_order_cancelled(7) means order 7 was removed from the book
    t.on_order_cancelled(id);
    
    // called when an ordered can not be placed into the order book
    // (order id, reason)
    // the reason says why, e.g. a bad price, zero quantity, or a duplicate id
    t.on_order_rejected(id, reason);
    
    // called whenever the total quantity waiting at a price level changes
    // (price, total quantity now waiting at that price, direction)
    // e.g. t.on_order_book_update(1000, 120, OrderDirection::Buy) means buyers at price 1000
    // now have 120 units waiting in total. this is is broadcasted as market data
    t.on_order_book_update(price, quantity, direction);

};

template <TradeListener Listener>
class OrderBook
{

public:
    
    OrderBook(Listener& listener) : 
        m_listener{ listener }, m_order_pool { 1'000'000 }, m_bids { MAX_PRICE + 1 }, 
        m_asks { MAX_PRICE + 1 }, m_order_indices (MAX_PRICE, -1) {}
    
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
    void submit_order(Order order);
    
    // removes a resting order by its id
    // looks the order up, unlinks it from its queue, frees its pool slot, and
    // rejects with not found if the id doesnt exist or already filled.
    void cancel_order();

private:

    // the highest price the book can hold. there is one slot in the levels arrays per price.
    static constexpr size_t MAX_PRICE { 1'000'000 };

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

    // a reference to the listener object 
    // the listener object is what the book tells when something happens
    Listener& m_listener;

    // a lookup by order_id; tells us which slot in the order pool holds a specific order
    // or -1 if it is not in the book, so that we can do something with it
    // e.g. cancel, check duplicate, etc.
    // e.g. m_order_indices[10] = 3 tells us the order with id 10 is stored in slot 3 of the order pool
    // given specifically for fast lookup; otherwise we would have to search the entire vector (O(n))
    std::vector<int32_t> m_order_indices {};

    // one level for every price, 1 array for each price
    // e.g. mbids[10] gives the level, or the buyers waiting at level of price 10.
    std::vector<Level> m_bids {};
    std::vector<Level> m_asks {};
    
    // the highest price a buyer is waiting at. 0 means no buyers, or the best bidding price
    int32_t m_max_bid_price { 0 };

    // the lowest price a seller is waiting at, or the best asking price
    int32_t m_min_ask_price { MAX_PRICE };

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
    void match_order(Order& order);

    // trades an incoming order against one level, one price.
    // takes orders from the front of the queue (oldest first, which is
    // price-time priority), trades min(incoming, resting) each time, and
    // removes resting orders that fully fill. stops when the incoming order is
    // done or the level is empty.
    // match_order walks the levels, match_with_level works inside one level.
    void match_with_level();

};

template <TradeListener Listener>
void OrderBook<Listener>::submit_order(Order order)
{

}

template <TradeListener Listener>
void OrderBook<Listener>::match_order(Order& order)
{

}

#endif // ORDER_BOOK_HH