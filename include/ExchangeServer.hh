#ifndef EXCHANGE_SERVER_HH
#define EXCHANGE_SERVER_HH

#include <maroof/net/Endpoint.hh>
#include <maroof/net/Connection.hh>
#include <maroof/net/Server.hh>

#include <thread>
#include <unordered_map>
#include <variant>
#include <span>
#include <cstddef>

#include "OrderBook.hh"
#include "ExchangeListener.hh"
#include "CommandQueue.hh"

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

    // maps endpoint -> connection
    // bridges servers on disconnect callback, which only gives an Endpoint to the listeners, unregister connection which needs a connection ptr.
    // in other words, we track the Connection* for each client when they connect, and look it up by endpoint when they disconnect
    std::unordered_map<maroof::net::Endpoint, maroof::net::Connection*> m_endpoint_to_connection_map;

    // the actual server that accepts incoming connections and dispatches the correct callback for a packet (e.g. on_trade)
    maroof::net::Server m_server;
    
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

};

inline ExchangeServer::ExchangeServer(const maroof::net::Endpoint& bind_addr) : m_server { bind_addr }, m_listener { }, m_book { m_listener }, m_queue { }
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
    m_server.run();

}

inline void ExchangeServer::stop()
{
    
    if (!m_matching_thread.joinable()) return;

    // the thread was not started or finished its work and is ready to join
    m_server.stop();
    m_queue.shut_down();
    m_matching_thread.join();

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
        m_queue.push(cancel_order_cmd);

    }

}

#endif // EXCHANGE_SERVER_HH