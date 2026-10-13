#ifndef ORDER_HH
#define ORDER_HH

#include <cstdint>

// this simply is the direction; buy -> or sell <-.
enum class OrderDirection
{

    Buy,
    Sell,

};

// a GTC order is one to buy or sell at a set price, called a "limit price"
// this order stays in the book (its a resting order; it needs to be matched
// by our matching engine) until it fully fills, or it is cancelled.
// any part that can not match right away just waits in line.
// in other words, get me this price, and i will wait as long as it takes to get it.

// a FAK order is one to  buy or sell whatever is available right now at your price OR better, then cancel the rest.
// it never sits in the book.
// in other words, buy or sell at my price or a better one, and then just dont buy or sell anything else.
// we do not keep this as a part of the order struct; instead we use it to dictate what to do
// FAK orders never sit in the book, so OrderType can just be passed in to decide how to handle it
// this keeps resting orders smaller
enum class OrderType
{

    GTC, // "Good Till Cancelled"
    FAK, // "Fill And Kill"

};

// when the exchange refuses an order, it tells the client why using one of
// these reasons. stored as a uint8_t (1 byte) to keep messages small.
enum class OrderRejected : uint8_t 
{

    InvalidPrice,    // price is outside the allowed range
    InvalidQuantity, // quantity is zero, which can't be a real order
    DuplicateId,     // an order with this id is already in the book
    FullPool,        // the preallocated pool has no free slots left
    NotFound,        // tried to cancel an order that doesn't exist or already filled

};

// struct representing an order 
// just needs to be a struct since it is a "blob" of information, has no real meaning if
// it is not associated with an orderbook.
// in other words, we do not really care if someone creates one of these for no reason (but please dont)
struct alignas(32) Order
{

    Order() = default;

    // this struct is just data; the engine will decide things like id, and fill the other stuff properly.
    Order(uint64_t id, int32_t price, int32_t quantity, OrderDirection direction) 
        : m_id {id}, m_price{price}, m_quantity{quantity}, m_direction{direction} {}

    ~Order() = default;

    // an order is just a blob of data as previously mentioned
    // for those reasons copying and moving is okay
    // moving especialyl since Order owns no heap allocs
    Order(const Order&) = default;
    Order& operator=(const Order&) = default;

    Order(Order&&) = default;
    Order& operator=(Order&&) = default;

    OrderDirection m_direction;

    uint64_t m_id {}; // uniquely represents an order; u64 is best for ids

    // prices stored as whole numbers of ticks, not float to avoid rounding error
    // needs to be signed; price can be negative
    // negative price just means the seller is paying the buyer to take the thing off their hands 
    // also makes subtraction not wrap around, so spreads and price differences are safe
    int32_t m_price {};

    // represents how many units (shares) of teh asset,, or the "security", 
    // the order wants to buy or sell. can't be negative, so it's unsigned.
    // unsigned subtraction wraps around, clamping needs to be present
    // when filling orders 
    uint32_t m_quantity {};

    // orders are linked into a line because each price level is a FIFO queue
    // and a linked list lets us cancel an order from the middle instantly
    // orders thus hold an index to their previous and next order in this "linked list"
    // these are not actual pointers for performance reasons (cache friendly)
    // -1 = none
    int32_t m_prev { -1 };
    int32_t m_next { -1 };

};

#endif // ORDER_HH