#ifndef EXCHANGE_LISTENER_HH
#define EXCHANGE_LISTENER_HH

#include <maroof/net/Connection.hh>
#include <unordered_map>
#include <unordered_set>
#include <span>
#include <cstddef>

#include "Order.hh"
#include "Protocol.hh"

// class that sits between the matching engine and the network
// whenever the engine says a trade happens, this class will be used to translate that into a packet and send it to the right client
// this class needs to be fast as it gives up to date information
class ExchangeListener
{
public:

    ExchangeListener() = default;
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

    // order id -> client connection (the client that owns this order id)
    std::unordered_map<uint64_t, maroof::net::Connection*> m_order_to_client_map;
    
    // set of all client connections used for broadcast
    std::unordered_set<maroof::net::Connection*> m_clients_set;

};

// sends a trade filled event to the aggressor and passive client. 
// aggressor is just the incoming order; the one that arrived and triggered the match
// passive is just the resting order
// also sends each what priec they traded at and quantity that was filled
inline void ExchangeListener::on_trade(uint64_t aggressor_id, uint64_t passive_id, int32_t price, uint32_t quantity, bool aggressor_filled, bool passive_filled)
{

    // define the events for each side
    TradeFillEvent aggressor_event;
    aggressor_event.your_order_id = aggressor_id;
    aggressor_event.counterparty_order_id = passive_id;
    aggressor_event.price = price;
    aggressor_event.quantity_filled = quantity;

    TradeFillEvent passive_event;
    passive_event.your_order_id = passive_id;
    passive_event.counterparty_order_id = aggressor_id;
    passive_event.price = price;
    passive_event.quantity_filled = quantity;

    // convert to std byte payloads for the send function
    std::span<const std::byte> aggressor_payload { reinterpret_cast<const std::byte*>(&aggressor_event), sizeof(aggressor_event) };
    std::span<const std::byte> passive_payload { reinterpret_cast<const std::byte*>(&passive_event), sizeof(passive_event) };

    // send it to the aggressor, i.e., the connection whose order was the incoming order that triggered the match
    m_order_to_client_map[aggressor_id]->send_reliable(aggressor_payload);

    // send it to the passive side, i.e., the connection whose order was already resting in the book  
    m_order_to_client_map[passive_id]->send_reliable(passive_payload);

    // the orders are fully filled; remove these ids from the orer to client map.
    if (aggressor_filled) m_order_to_client_map.erase(aggressor_id);
    if (passive_filled) m_order_to_client_map.erase(passive_id);
    
}

// sends an ack event to the client that submitted an order, when that order results in resting orders
inline void ExchangeListener::on_order_added(uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction)
{

    AckEvent event;
    event.order_id = id;
    std::span<const std::byte> payload { reinterpret_cast<const std::byte*>(&event), sizeof(event) };

    m_order_to_client_map[id]->send_reliable(payload);

}
    
// sends a cancel confirmation to the client when their resting order is successfully cancelled
inline void ExchangeListener::on_order_cancelled(uint64_t id)
{

    CancelEvent event;
    event.order_id = id;
    std::span<const std::byte> payload { reinterpret_cast<const std::byte*>(&event), sizeof(event) };

    m_order_to_client_map[id]->send_reliable(payload);
    m_order_to_client_map.erase(id);

}

// sends a rejected event and reason to the client that whose order was rejected by the matching engine
inline void ExchangeListener::on_order_rejected(uint64_t id, OrderRejected reason)
{

    RejectEvent event;
    event.order_id = id;
    event.reason = reason;
    std::span<const std::byte> payload { reinterpret_cast<const std::byte*>(&event), sizeof(event) };

    m_order_to_client_map[id]->send_reliable(payload);
    m_order_to_client_map.erase(id);

}

// broadcasts a bookupdate event to the client whenever a price level's total volume changes
inline void ExchangeListener::on_order_book_update(int32_t price, uint32_t quantity, OrderDirection direction)
{

    BookUpdateEvent event;
    event.price = price;
    event.volume = quantity;
    event.direction = direction;

    std::span<const std::byte> payload { reinterpret_cast<const std::byte*>(&event), sizeof(event) };

    for (auto* conn : m_clients_set)
    {

        conn->send_sequenced(payload);

    }

}

inline void ExchangeListener::register_connection(maroof::net::Connection* conn)
{

    m_clients_set.insert(conn);

}   

inline void ExchangeListener::unregister_connection(maroof::net::Connection* conn)
{

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

    m_order_to_client_map[id] = conn;

}
    
#endif // EXCHANGE_LISTENER_HH