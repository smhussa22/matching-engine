#ifndef EXCHANGE_LISTENER_HH
#define EXCHANGE_LISTENER_HH

#include <maroof/net/Server.hh>
#include <maroof/net/Connection.hh>
#include <unordered_map>
#include <unordered_set>

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
    void on_trade(uint64_t aggressor_id, uint64_t passive_id, int32_t price, uint32_t quantity);
    
    // sends an ack event to the client that submitted an order, when that order results in resting orders
    void on_order_added(uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction);
    
    // sends a cancel confirmation to the client when their resting order is successfully cancelled
    void on_order_cancelled(uint64_t id);

    // sends a rejected event and reason to the client that whose order was rejected by the matching engine
    void on_order_rejected(uint64_t id, OrderRejected reason);

    // broadcasts a bookupdate event to the client whenever a price level's total volume changes
    void on_order_book_update(int32_t price, uint32_t quantity, OrderDirection direction);

private:

    // order id -> client connection (the client that owns this order id)
    std::unordered_map<uint64_t, maroof::net::Connection*> m_order_to_client_map;
    
    // set of all client connections used for broadcast
    std::unordered_set<maroof::net::Connection*> m_clients_set;

};

void ExchangeListener::on_trade(uint64_t aggressor_id, uint64_t passive_id, int32_t price, uint32_t quantity)
{

}
    
void ExchangeListener::on_order_added(uint64_t id, int32_t price, uint32_t quantity, OrderDirection direction)
{

}
    
void ExchangeListener::on_order_cancelled(uint64_t id)
{

}
    
void ExchangeListener::on_order_rejected(uint64_t id, OrderRejected reason)
{

}
    
void ExchangeListener::on_order_book_update(int32_t price, uint32_t quantity, OrderDirection direction)
{

}
    
#endif // EXCHANGE_LISTENER_HH