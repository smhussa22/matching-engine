#ifndef EXCHANGE_SERVER_HH
#define EXCHANGE_SERVER_HH

#include <maroof/net/Endpoint.hh>
#include <maroof/net/Connection.hh>

#include <thread>
#include <unordered_map>
#include <variant>

// class for the exchange server
// this is the class that bridges listeners with the order book; tiues the entire exchange together
// it handles things like ..., ..., ..., etc.
// it owns the network server, the order book, the listener, the command queue, and the matching thread
// it handles things like accepting incoming connections from clients and decoding their packets into commands
// it feeds these commands to our single matching thread
// it also routes callbacks from the engine, like on_order_cancelled for example, to the right clients
// the network callback path and the matching loop are both invoked very often and thus need to be pretty fast
// most wokr needs to be done outside of the matching loop and network callback paths when possible for this reason    
class ExchangeServer
{
public:

    // binds the given endpoint, builds all members, and wires the three server call backs
    ExchangeServer(const maroof::net::Endpoint& bind_addr);

    // calls stop if start was called
    ~ExchangeServer();

    ExchangeServer(const ExchangeServer&) = delete;
    ExchangeServer& operator=(const ExchangeServer&) = delete;

    ExchangeServer(ExchangeServer&&) = delete;
    ExchangeServer& operator=(ExchangeServer&&) = delete;

    // launches the matching thread then runs the servers blocking packet loop  
    // must be called by a thread you are OK with blocking, because run() does not return until stop()
    // is caled from another thread.
    void start();

    // signals the server to exit its loop, drains the queue by waking the matching thread, then joins the thread (wait and clean)
    void stop();

private:

    // the singular thread used to match orders
    std::thread m_matching_thread;

    // maps endpoint -> connection
    // bridges servers on disconnect callback, which only gives an Endpoint to the listeners, unregister connection which needs a connection ptr.
    // in other words, we track the Connection* for each client when they connect, and look it up by endpoint when they disconnect
    std::unordered_map<maroof::net::Endpoint, maroof::net::Connection*> m_endpoint_to_connection_map;

    // the function that the matching thread
    void matching_loop();

    // callback used by the server for each incoming packet to decode the type tag and push the cmd onto the queue
    void on_packet_received(maroof::net::Connection& conn, std::span<const std::byte> payload);

};

void ExchangeServer::start()
{

}

void ExchangeServer::stop()
{
    
}

void ExchangeServer::matching_loop()
{
    
}

void ExchangeServer::on_packet_received(maroof::net::Connection& conn, std::span<const std::byte> payload)
{
    
}

#endif // EXCHANGE_SERVER_HH