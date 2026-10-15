#ifndef ORDER_BOOK_HH
#define ORDER_BOOK_HH

#include "OrderPool.hh"
#include "Order.hh"

#include <concepts>
#include <algorithm>
#include <ostream>

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
concept TradeListener = requires(T t, uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction, OrderRejected reason, bool aggressor_filled, bool passive_filled)
{

    // called when two orders are matched
    // (incoming id, resting id, price, quantity, aggressor_filled, passive_filled)
    // carries both ids so both sides can be told via one of the concept functions 
    // reports whether the aggressor or passive side are fully filled or not
    // e.g. t.on_trade(7, 3, 1000, 50) means incoming order 7 traded with resting order 3
    // for 50 units at price 1000
    t.on_trade(id, id, price, quantity, aggressor_filled, passive_filled);
    
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
    void submit_order(Order order, OrderType order_type);
    
    // removes a resting order by its id
    // looks the order up, unlinks it from its queue, frees its pool slot, and
    // rejects with not found if the id doesnt exist or already filled.
    void cancel_order(uint64_t order_id);

    // writes a human-readable snapshot of the book to the given stream.
    // asks are printed high to low on top, bids high to low below, so the spread sits in the middle.
    void print(std::ostream& os) const
    {
        os << "--- ASKS ---\n";
        for (int32_t p { static_cast<int32_t>(MAX_PRICE) }; p >= 0; --p)
        {
            if (m_asks[p].m_total_volume > 0)
                os << p << "\t|" << m_asks[p].m_total_volume << '\n';
        }

        os << "--- BIDS ---\n";
        for (int32_t p { static_cast<int32_t>(MAX_PRICE) }; p >= 0; --p)
        {
            if (m_bids[p].m_total_volume > 0)
                os << p << "\t|" << m_bids[p].m_total_volume << '\n';
        }
        os << "------------\n";
    }

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
    void add_order(const Order& order);

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
    void match_with_level(Order& order, Level& level, int32_t price);

};

// stream insertion lives outside the class so `std::cout << book` works naturally
template <TradeListener Listener>
std::ostream& operator<<(std::ostream& os, const OrderBook<Listener>& book)
{
    book.print(os);
    return os;
}

template <TradeListener Listener>
void OrderBook<Listener>::submit_order(Order order, OrderType order_type)
{

    if (order.m_quantity <= 0)
    {

        m_listener.on_order_rejected(order.m_id, OrderRejected::InvalidQuantity);
        return;

    }

    if (order.m_price <= 0 || order.m_price > MAX_PRICE)
    {

        m_listener.on_order_rejected(order.m_id, OrderRejected::InvalidPrice);
        return;

    }

    if (order.m_id >= m_order_indices.size())
    {

        m_listener.on_order_rejected(order.m_id, OrderRejected::InvalidPrice);
        return;

    }

    if (m_order_indices[order.m_id] != -1)
    {

        m_listener.on_order_rejected(order.m_id, OrderRejected::DuplicateId);
        return;

    }

    match_order(order);
    if (order.m_quantity > 0 && order_type == OrderType::GTC)
    {

        add_order(order);

    }

}

template <TradeListener Listener>
void OrderBook<Listener>::cancel_order(uint64_t order_id)
{

    auto order_to_cancel_index { m_order_indices[order_id] };
    if (order_to_cancel_index == -1 || order_id >= m_order_indices.size())
    {

        m_listener.on_order_rejected(order_id, OrderRejected::NotFound);
        return;

    }

    // look up the order in the pool
    Order& order { m_order_pool.get(order_to_cancel_index) };
    // get a REFERENCE to the order direction's level array, the one this oen is in
    std::vector<Level>& book_direction
    { 

        [&]() -> std::vector<Level>& 
        {

            if (order.m_direction == OrderDirection::Buy)
            {

                return m_bids;

            }
            else
            {
                return m_asks;
            }

        }()

    };

    // since were removing this order, unaccount it from its level struct.
    Level& level { book_direction[order.m_price] };
    level.m_total_volume -= order.m_quantity;

    // update listeners on order book update.
    m_listener.on_order_book_update(order.m_price, level.m_total_volume, order.m_direction);

    // unlink the order from its queue, and free up its pool slot
    if (order.m_prev != -1)
    {
        m_order_pool.get(order.m_prev).m_next = order.m_next;
    }
    else
    {
        level.m_head = order.m_next;
    }

    if (order.m_next != -1)
    {

        m_order_pool.get(order.m_next).m_prev = order.m_prev;

    }
    else
    {
        level.m_tail = order.m_prev;
    }

    m_order_pool.deallocate(order_to_cancel_index);
    m_order_indices[order_id] = -1;
    m_listener.on_order_cancelled(order_id);

}

template <TradeListener Listener>
void OrderBook<Listener>::add_order(const Order& order)
{

    int32_t order_to_add_index { m_order_pool.allocate(order.m_id, order.m_price, order.m_quantity, order.m_direction) };
    if (order_to_add_index == -1)
    {

        m_listener.on_order_rejected(order.m_id, OrderRejected::FullPool);
        return;

    }

    std::vector<Level>& book_direction
    { 

        [&]() -> std::vector<Level>& 
        {

            if (order.m_direction == OrderDirection::Buy)
            {

                return m_bids;

            }
            else
            {
                return m_asks;
            }

        }()

    };

    Level& level { book_direction[order.m_price] };
    level.m_total_volume += order.m_quantity;
    m_order_indices[order.m_id] = order_to_add_index;

    if (level.m_head == -1)
    {

        level.m_head = order_to_add_index;
        level.m_tail = order_to_add_index;

    }
    else
    {

        m_order_pool.get(level.m_tail).m_next = order_to_add_index;
        m_order_pool.get(order_to_add_index).m_prev = level.m_tail;
        level.m_tail = order_to_add_index;

    }

    if (order.m_direction == OrderDirection::Buy)
    {

        if (order.m_price > m_max_bid_price)
        {

            m_max_bid_price = order.m_price;

        }

    }
    else
    {

        if (order.m_price < m_min_ask_price)
        {

            m_min_ask_price = order.m_price;
        
        }

    }

    m_listener.on_order_added(order.m_id, order.m_price, order.m_quantity, order.m_direction);
    m_listener.on_order_book_update(order.m_price, level.m_total_volume, order.m_direction);

}

template <TradeListener Listener>
void OrderBook<Listener>::match_order(Order& order)
{

    // we are trying to match a buy order with the opposite side of the book
    // fill it by eating throuhg the cheapest available seller
    // while the order still has quantity left to fill & 
    // the cheapest ask price is low enough to be acceptable, trade at that price 
    if(order.m_direction == OrderDirection::Buy)
    {

        while (order.m_quantity > 0 && order.m_price >= m_min_ask_price)
        {

            // if the minimum asking price is greater than the max price
            // just stop trying to match
            if (m_min_ask_price > static_cast<int32_t>(MAX_PRICE)) 
            {
            
                break;
            
            }

            Level& level { m_asks[m_min_ask_price] };

            if(level.m_head == -1)
            {

                m_min_ask_price++;

            }
            else
            {

                match_with_level(order, level, m_min_ask_price);
                if (level.m_head == -1)
                {

                    m_min_ask_price++;

                }

            }
            
        }

    }
    else
    {

        while (order.m_quantity > 0 && order.m_price <= m_max_bid_price)
        {

            if (m_max_bid_price < 0) 
            {
            
                break;
            
            }

            Level& level { m_bids[m_max_bid_price] };

            if(level.m_head == -1)
            {

                m_max_bid_price--;

            }
            else
            {

                match_with_level(order, level, m_max_bid_price);
                if (level.m_head == -1)
                {

                    m_max_bid_price--;

                }

            }

        }

    }

}

template <TradeListener Listener>
void OrderBook<Listener>::match_with_level(Order& order, Level& level, int32_t price)
{

    // while the level to match this wit hstill has orders to match it with and
    // the order still has a quantity left to fill
    
    uint32_t quantity_traded {};

    while (level.m_head != -1 && order.m_quantity > 0)
    {

        int32_t curr_order_index { level.m_head };
        Order& resting_order { m_order_pool.get(curr_order_index) };

        // the quantity we will trade is the minimum between what this incoming
        // order has to fill, or the resting orders available quantity
        quantity_traded = std::min(order.m_quantity, resting_order.m_quantity);

        order.m_quantity -= quantity_traded;
        resting_order.m_quantity -= quantity_traded;
        level.m_total_volume -= quantity_traded;

        m_listener.on_trade(order.m_id, resting_order.m_id, price, quantity_traded, order.m_quantity == 0, resting_order.m_quantity == 0);
        m_listener.on_order_book_update(price, level.m_total_volume, resting_order.m_direction);

        if (resting_order.m_quantity == 0)
        {

            int32_t next_index { resting_order.m_next };
            level.m_head = next_index;

            if (next_index != -1)
            {

                m_order_pool.get(next_index).m_prev = -1;

            }
            else
            {

                level.m_tail = -1;

            }

            m_order_indices[resting_order.m_id] = -1;
            m_order_pool.deallocate(curr_order_index);

        }

    }

}

#endif // ORDER_BOOK_HH