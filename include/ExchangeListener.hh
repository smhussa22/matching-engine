#ifndef EXCHANGE_LISTENER_HH
#define EXCHANGE_LISTENER_HH

#include <maroof/net/Connection.hh>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Order.hh"
#include "OutboundQueue.hh"
#include "Protocol.hh"

// class that sits between the matching engine and the network
// whenever the engine says a trade happens, this class will be used to translate that into a packet and send it to the right client
// this class needs to be fast as it gives up to date information
class ExchangeListener
{
public:

    // outbound queue is the one-way channel back to the server thread for sending bytes.
    // the listener never calls Connection::send_* directly to avoid racing with the
    // server's internal retransmit loop. nullable so tests can skip the queue entirely.
    explicit ExchangeListener(OutboundQueue* outbound = nullptr) : m_outbound { outbound } {}
    ~ExchangeListener() = default;

    ExchangeListener(const ExchangeListener&) = delete;
    ExchangeListener& operator=(const ExchangeListener&) = delete;

    ExchangeListener(ExchangeListener&&) = delete;
    ExchangeListener& operator=(ExchangeListener&&) = delete;

    // sends a trade filled event to the aggressor and passive client. 
    // aggressor is just the incoming order; the one that arrived and triggered the match
    // passive is just the resting order
    // also sends each what priec they traded at and quantity that was filled
    void on_trade(uint64_t aggressor_id, uint64_t passive_id, int32_t price, uint32_t quantity, bool aggressor_filled, bool passive_filled);
    
    // sends an ack event to the client that submitted an order, when that order results in resting orders
    void on_order_added(uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction);
    
    // sends a cancel confirmation to the client when their resting order is successfully cancelled
    void on_order_cancelled(uint64_t id);

    // sends a rejected event and reason to the client that whose order was rejected by the matching engine
    void on_order_rejected(uint64_t id, OrderRejected reason);

    // broadcasts a bookupdate event to the client whenever a price level's total volume changes
    void on_order_book_update(int32_t price, uint32_t quantity, OrderDirection direction);

    void register_connection(maroof::net::Connection* conn);                                                                    
    void unregister_connection(maroof::net::Connection* conn);
    
    // called when a new order command is sent in by a client
    void register_order(uint64_t id, maroof::net::Connection* conn);

    // introspection helpers (used by tests)
    size_t order_count()  const noexcept { return m_order_to_client_map.size(); }
    size_t client_count() const noexcept { return m_clients_set.size(); }
    bool   contains_order (uint64_t id)                    const noexcept { return m_order_to_client_map.contains(id); }
    bool   contains_client(maroof::net::Connection* conn)  const noexcept { return m_clients_set.contains(conn); }

private:

    // helper: serializes any trivially-copyable event into owned bytes and pushes to the outbound queue.
    // the server thread will drain the queue and actually call send_* on the connection.
    template <typename EventT>
    void emit(maroof::net::Connection* conn, const EventT& event, OutboundQueue::Channel channel)
    {
        if (!m_outbound || !conn) return;
        std::vector<std::byte> bytes(sizeof(EventT));
        std::memcpy(bytes.data(), &event, sizeof(EventT));
        m_outbound->push({ conn, std::move(bytes), channel });
    }

    // guards the two maps. the server thread (via register_* / unregister_connection) and the
    // matching thread (via the five engine callbacks) both touch the state behind this mutex.
    mutable std::mutex m_mutex;

    // order id -> client connection (the client that owns this order id)
    std::unordered_map<uint64_t, maroof::net::Connection*> m_order_to_client_map;

    // set of all client connections used for broadcast
    std::unordered_set<maroof::net::Connection*> m_clients_set;

    // where outbound events are posted for the server thread to actually transmit.
    OutboundQueue* m_outbound;

    // monotonically increasing counter stamped onto every outbound event.
    // both sides of a trade share the same seq — they represent one event.
    uint64_t m_seq { 0 };

};

// sends a trade filled event to the aggressor and passive client. 
// aggressor is just the incoming order; the one that arrived and triggered the match
// passive is just the resting order
// also sends each what priec they traded at and quantity that was filled
inline void ExchangeListener::on_trade(uint64_t aggressor_id, uint64_t passive_id, int32_t price, uint32_t quantity, bool aggressor_filled, bool passive_filled)
{

    std::lock_guard<std::mutex> lock { m_mutex };

    const uint64_t seq { ++m_seq };

    TradeFillEvent aggressor_event {};
    aggressor_event.seq                   = seq;
    aggressor_event.your_order_id         = aggressor_id;
    aggressor_event.counterparty_order_id = passive_id;
    aggressor_event.price                 = price;
    aggressor_event.quantity_filled       = quantity;

    TradeFillEvent passive_event {};
    passive_event.seq                   = seq;
    passive_event.your_order_id         = passive_id;
    passive_event.counterparty_order_id = aggressor_id;
    passive_event.price                 = price;
    passive_event.quantity_filled       = quantity;

    if (auto it = m_order_to_client_map.find(aggressor_id); it != m_order_to_client_map.end())
        emit(it->second, aggressor_event, OutboundQueue::Channel::Reliable);

    if (auto it = m_order_to_client_map.find(passive_id); it != m_order_to_client_map.end())
        emit(it->second, passive_event, OutboundQueue::Channel::Reliable);

    if (aggressor_filled) m_order_to_client_map.erase(aggressor_id);
    if (passive_filled)   m_order_to_client_map.erase(passive_id);

}

// sends an ack event to the client that submitted an order, when that order results in resting orders
inline void ExchangeListener::on_order_added(uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction)
{

    std::lock_guard<std::mutex> lock { m_mutex };

    AckEvent event {};
    event.seq      = ++m_seq;
    event.order_id = id;

    if (auto it = m_order_to_client_map.find(id); it != m_order_to_client_map.end())
        emit(it->second, event, OutboundQueue::Channel::Reliable);

}

// sends a cancel confirmation to the client when their resting order is successfully cancelled
inline void ExchangeListener::on_order_cancelled(uint64_t id)
{

    std::lock_guard<std::mutex> lock { m_mutex };

    CancelEvent event {};
    event.seq      = ++m_seq;
    event.order_id = id;

    if (auto it = m_order_to_client_map.find(id); it != m_order_to_client_map.end())
    {
        emit(it->second, event, OutboundQueue::Channel::Reliable);
        m_order_to_client_map.erase(it);
    }

}

// sends a rejected event and reason to the client that whose order was rejected by the matching engine
inline void ExchangeListener::on_order_rejected(uint64_t id, OrderRejected reason)
{

    std::lock_guard<std::mutex> lock { m_mutex };

    RejectEvent event {};
    event.seq      = ++m_seq;
    event.order_id = id;
    event.reason   = reason;

    if (auto it = m_order_to_client_map.find(id); it != m_order_to_client_map.end())
    {
        emit(it->second, event, OutboundQueue::Channel::Reliable);
        m_order_to_client_map.erase(it);
    }

}

// broadcasts a bookupdate event to the client whenever a price level's total volume changes
inline void ExchangeListener::on_order_book_update(int32_t price, uint32_t quantity, OrderDirection direction)
{

    std::lock_guard<std::mutex> lock { m_mutex };

    BookUpdateEvent event {};
    event.seq       = ++m_seq;
    event.price     = price;
    event.volume    = quantity;
    event.direction = direction;

    // one owned-bytes copy per client so the server thread can send each independently
    for (auto* conn : m_clients_set)
        emit(conn, event, OutboundQueue::Channel::Sequenced);

}

inline void ExchangeListener::register_connection(maroof::net::Connection* conn)
{

    std::lock_guard<std::mutex> lock { m_mutex };
    m_clients_set.insert(conn);

}

inline void ExchangeListener::unregister_connection(maroof::net::Connection* conn)
{

    std::lock_guard<std::mutex> lock { m_mutex };
    m_clients_set.erase(conn);

    // todo: maybe find a better approach
    for (auto it { m_order_to_client_map.begin() }; it != m_order_to_client_map.end(); )
    {

        if (it->second == conn)
        {
            it = m_order_to_client_map.erase(it);
        }
        else
        {
            ++it;
        }

    }
}    

inline void ExchangeListener::register_order(uint64_t id, maroof::net::Connection* conn)
{

    std::lock_guard<std::mutex> lock { m_mutex };
    m_order_to_client_map[id] = conn;

}
    
#endif // EXCHANGE_LISTENER_HH