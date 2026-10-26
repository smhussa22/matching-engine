#ifndef PROTOCOL_HH
#define PROTOCOL_HH

#include "Order.hh"
#include <cstdint>
#include <variant>


// inbound message
enum class CommandType : uint8_t
{

    NewOrder,
    CancelOrder,

};

struct NewOrderCmd
{

    CommandType type { CommandType::NewOrder };
    uint64_t order_id;
    int32_t price;
    uint32_t quantity;
    OrderDirection direction;
    OrderType order_type;

};

struct CancelOrderCmd
{

    CommandType type { CommandType::CancelOrder };
    uint64_t order_id;

};

using Command = std::variant<NewOrderCmd, CancelOrderCmd>;

// outbound message
enum class EventType : uint8_t
{

    Ack,        // order accepted (resting or being processed)
    Reject,     // order rejected
    TradeFill,  // all or part of order matched against a counterparty, holds fill price and qty
    BookUpdate, // broadcasted because order book was changed
    Cancelled,  // the order was cancelled by the engine

};

// sent when a GTC order is accepted and resting in the book
struct AckEvent
{

    EventType type { EventType::Ack };
    uint64_t seq;
    uint64_t order_id;

};

// sent when an order is refused, with the reason why
struct RejectEvent
{

    EventType type { EventType::Reject };
    uint64_t seq;
    uint64_t order_id;
    OrderRejected reason;

};

// sent privately to both sides of a trade when a match occurs
// both sides carry the same seq — they represent one trade event
struct TradeFillEvent
{

    EventType type { EventType::TradeFill };
    uint64_t seq;
    uint64_t your_order_id;
    uint64_t counterparty_order_id;
    int32_t price;
    uint32_t quantity_filled;

};

// broadcast to all clients whenever a price levels volume changes
struct BookUpdateEvent
{

    EventType type { EventType::BookUpdate };
    uint64_t seq;
    int32_t price;
    uint32_t volume;
    OrderDirection direction;

};

struct CancelEvent
{

    EventType type { EventType::Cancelled };
    uint64_t seq;
    uint64_t order_id;

};

#endif // PROTOCOL_HH