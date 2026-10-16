#ifndef EXCHANGE_SERVER_HH
#define EXCHANGE_SERVER_HH

#include <maroof/net/Endpoint.hh>
#include <maroof/net/Connection.hh>
#include <maroof/net/Server.hh>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <span>
#include <thread>
#include <unordered_map>
#include <variant>

#include "CommandQueue.hh"
#include "ExchangeListener.hh"
#include "OrderBook.hh"
#include "OutboundQueue.hh"

// class for the exchange server
// this is the class that ties the exchange together.                                                                                                
// owns the network server, the order book, the listener, the command queue, and the thread used to match orders.
// network callbacks decode incoming packets into commands and push them onto the queue;                                                            
// the matching thread drains the queue and dispatches to the book.
// expensive work needs to stay off the network callbacks (e.g. on_receive) and the matching thraed

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

    // signals our own tick loop (see start()) to exit. the server's internal m_running isn't used
    // because we drive its tick ourselves instead of calling run().
    std::atomic<bool> m_running { false };

    // maps endpoint -> connection
    // bridges servers on disconnect callback, which only gives an Endpoint to the listeners, unregister connection which needs a connection ptr.
    // in other words, we track the Connection* for each client when they connect, and look it up by endpoint when they disconnect
    std::unordered_map<maroof::net::Endpoint, maroof::net::Connection*> m_endpoint_to_connection_map;

    // the actual server that accepts incoming connections and dispatches the correct callback for a packet (e.g. on_trade)
    maroof::net::Server m_server;

    // outbound queue — the matching thread serializes events into here; our tick loop drains them
    // and actually calls Connection::send_* from the server thread (safe).
    OutboundQueue m_outbound;

    // sits between the matching engine and the network; translates packets from events for the right clients
    ExchangeListener m_listener;

    // stores buy and sell orders when necessary
    OrderBook<ExchangeListener> m_book;

    // queue of commands sent by clients ready to be processed by the matching thread
    CommandQueue m_queue;

    // the function that the matching thread uses to match orders in the queue and dispatch a command to the book
    // does so until shut down
    void matching_loop();

    // callback used by the server for each incoming packet to decode the type tag and push the cmd onto the queue
    void on_packet_received(maroof::net::Connection& conn, std::span<const std::byte> payload);

    // drains the outbound queue and calls send_reliable / send_sequenced on the stored Connection*.
    // only called from the server thread, so it never races with the server's internal retransmit loop.
    // returns true if at least one event was sent, false if the queue was empty.
    bool drain_outbound();

};

inline ExchangeServer::ExchangeServer(const maroof::net::Endpoint& bind_addr)
    : m_server   { bind_addr }
    , m_outbound {}
    , m_listener { &m_outbound }
    , m_book     { m_listener }
    , m_queue    {}
{

    m_server.on_connect(
        [this](maroof::net::Connection& conn) 
        { 
            
            m_endpoint_to_connection_map[conn.peer()] = &conn; 
            m_listener.register_connection(&conn);

        }
    );

    m_server.on_disconnect(
        [this](const maroof::net::Endpoint& endpoint)
        {

            auto it { m_endpoint_to_connection_map.find(endpoint) };
            if (it == m_endpoint_to_connection_map.end())
            {

                return;
            
            }

            m_listener.unregister_connection(it->second);
            m_endpoint_to_connection_map.erase(it);

        }
    );

    m_server.on_receive(
        [this](maroof::net::Connection& conn, std::span<const std::byte> payload, std::uint8_t channel)
        {

            on_packet_received(conn, payload);

        }
    );

}

inline ExchangeServer::~ExchangeServer()
{

    stop();

}

inline void ExchangeServer::start()
{

    m_matching_thread = std::thread { [this] { matching_loop(); } };

    // our own loop instead of m_server.run() so we can interleave drain_outbound() between ticks.
    // this keeps all Connection::send_* calls on this (server) thread — the matching thread never
    // touches a Connection directly, which is what the networking library requires.
    m_running.store(true);
    while (m_running.load(std::memory_order_relaxed))
    {
        // tick returns the number of packets processed. drain_outbound sends anything the matching
        // thread has queued. if BOTH did no work this iteration, we're idle — sleep briefly so we
        // don't burn 100% cpu polling. if either did work, immediately loop again at full speed.
        const std::size_t pkts     = m_server.tick();
        const bool        sent_any = drain_outbound();
        if (pkts == 0 && !sent_any)
            std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

}

inline void ExchangeServer::stop()
{

    if (!m_matching_thread.joinable()) return;

    // breaks our tick loop in start() so start() returns; stops server internals; then wakes
    // the matching thread so it can exit and be joined.
    m_running.store(false);
    m_server.stop();
    m_queue.shut_down();
    m_matching_thread.join();

}

inline bool ExchangeServer::drain_outbound()
{
    bool sent_any = false;
    while (auto event = m_outbound.try_pop())
    {
        sent_any = true;
        std::span<const std::byte> payload { event->payload.data(), event->payload.size() };

        // send_* on the connection serializes the payload into channel-owned storage and returns
        // the wire bytes to transmit. we MUST hand each span to socket.send_to immediately —
        // the next call to send_* on the same channel invalidates the previous spans.
        if (event->channel == OutboundQueue::Channel::Reliable)
        {
            auto wires = event->conn->send_reliable(payload);
            for (auto bytes : wires)
                (void) m_server.socket().send_to(bytes, event->conn->peer());
        }
        else
        {
            auto bytes = event->conn->send_sequenced(payload);
            (void) m_server.socket().send_to(bytes, event->conn->peer());
        }
    }
    return sent_any;
}

inline void ExchangeServer::matching_loop()
{
    
    for(;;)
    {

        auto command { m_queue.pop() };
        if (!command.has_value())
        {

            break;
        
        }

        // visit the submit or cancel order function depending on whats in the variant
        std::visit(
            [this](auto& command)
            {

                using T = std::decay_t<decltype(command)>;

                if constexpr (std::is_same_v<T, NewOrderCmd>)
                {

                    Order order { command.order_id, command.price, static_cast<int32_t>(command.quantity), command.direction };
                    m_book.submit_order(order, command.order_type);

                }
                else if constexpr (std::is_same_v<T, CancelOrderCmd>)
                {

                    m_book.cancel_order(command.order_id);

                }

            }, *command

        );

    }

}

inline void ExchangeServer::on_packet_received(maroof::net::Connection& conn, std::span<const std::byte> payload)
{
    
    if (payload.empty())
    {

        return;
    
    }

    // review the first byte to see if it is a new or cancel order command
    auto payload_type { static_cast<CommandType>(payload[0]) };

    // callback used by the server for each incoming packet to decode the type tag and push the cmd onto the queue
    if (payload_type == CommandType::NewOrder)
    {

        if (payload.size() < sizeof(NewOrderCmd))
        {

            return;

        }

        NewOrderCmd new_order_cmd;
        std::memcpy(&new_order_cmd, payload.data(), sizeof(new_order_cmd));
        m_listener.register_order(new_order_cmd.order_id, &conn);
        m_queue.push(new_order_cmd);

    }
    else if (payload_type == CommandType::CancelOrder) 
    {

        if (payload.size() < sizeof(CancelOrderCmd)) 
        {

            return;

        }

        CancelOrderCmd cancel_order_cmd;
        std::memcpy(&cancel_order_cmd, payload.data(), sizeof(cancel_order_cmd));
        // register so we can route a NotFound rejection back to the sender if the order never existed
        m_listener.register_order(cancel_order_cmd.order_id, &conn);
        m_queue.push(cancel_order_cmd);

    }

}

#endif // EXCHANGE_SERVER_HH