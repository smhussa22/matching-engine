#ifndef OUTBOUND_QUEUE_HH
#define OUTBOUND_QUEUE_HH

#include <maroof/net/Connection.hh>

#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

// bridges matching thread -> server thread for outbound packets.
//
// the matching thread must not call Connection::send_* directly: that races with the server
// thread's internal retransmit / receive loop, which also mutates the connection's channel
// state. instead the matching thread serializes each event into owned bytes and pushes it
// here; the server thread drains the queue in its own loop and calls send_reliable /
// send_sequenced from the "correct" thread.
class OutboundQueue
{
public:

    enum class Channel : uint8_t
    {

        Reliable,
        Sequenced,

    };

    struct Event
    {

        maroof::net::Connection* conn;
        std::vector<std::byte>   payload;   // owned so the sender can discard the source
        Channel                  channel;

    };

    OutboundQueue() = default;
    ~OutboundQueue() = default;

    OutboundQueue(const OutboundQueue&) = delete;
    OutboundQueue& operator=(const OutboundQueue&) = delete;

    OutboundQueue(OutboundQueue&&) = delete;
    OutboundQueue& operator=(OutboundQueue&&) = delete;

    void push(Event event)
    {

        std::lock_guard<std::mutex> lock { m_mutex };
        m_queue.push(std::move(event));

    }

    std::optional<Event> try_pop()
    {

        std::lock_guard<std::mutex> lock { m_mutex };
        if (m_queue.empty()) return std::nullopt;
        Event e { std::move(m_queue.front()) };
        m_queue.pop();
        return e;

    }

private:

    std::mutex m_mutex;
    std::queue<Event> m_queue;

};

#endif // OUTBOUND_QUEUE_HH
