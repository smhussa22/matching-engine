// synthetic client that drives the exchange server through maroof-net.
// pre-generates a workload, pipelines commands over the reliable channel,
// matches each response back to the sent command, and records round-trip
// latency into an HdrHistogram.
//
// run the server first:   ./build/server
// then in another term:   ./build/synth_client

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>
#include <thread>
#include <unordered_map>
#include <variant>

#include <hdr/hdr_histogram.h>
#include <maroof/net/Client.hh>
#include <maroof/net/Endpoint.hh>

#include "OrderGenerator.hh"
#include "Protocol.hh"

int main(int argc, char** argv)
{

    constexpr uint64_t SEED     = 42;
    constexpr size_t   PIPELINE = 32;   // how many outstanding commands at once

    size_t N = 100'000;
    if (argc > 1) N = static_cast<size_t>(std::atoll(argv[1]));

    std::println("pre-generating {} commands...", N);
    OrderGenerator gen { N, SEED };

    maroof::net::Endpoint server_addr { "127.0.0.1", 9000 };
    maroof::net::Client   client      { server_addr };

    hdr_histogram* histogram = nullptr;
    if (hdr_init(1, 10'000'000'000, 3, &histogram) != 0)
    {
        std::println("hdr_init failed");
        return 1;
    }

    // tracks when each command was sent, keyed by its order id.
    // when a response arrives referring to that id, we compute the rtt and erase.
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> send_times;
    send_times.reserve(PIPELINE * 2);

    std::atomic<bool>   connected    { false };
    std::atomic<bool>   disconnected { false };
    std::atomic<size_t> responses    { 0 };
    std::atomic<size_t> broadcasts   { 0 };

    client.on_connect([&]
    {
        std::println("connected to server");
        connected.store(true);
    });

    client.on_disconnect([&]
    {
        std::println("disconnected from server");
        disconnected.store(true);
    });

    client.on_receive([&](std::span<const std::byte> payload, std::uint8_t /*channel*/)
    {
        if (payload.empty()) return;

        auto t_recv = std::chrono::steady_clock::now();
        auto type   = static_cast<EventType>(payload[0]);

        uint64_t id = 0;
        bool     have_id = true;

        switch (type)
        {
            case EventType::Ack:
                if (payload.size() < sizeof(AckEvent)) return;
                { AckEvent e; std::memcpy(&e, payload.data(), sizeof(e)); id = e.order_id; }
                break;
            case EventType::Reject:
                if (payload.size() < sizeof(RejectEvent)) return;
                { RejectEvent e; std::memcpy(&e, payload.data(), sizeof(e)); id = e.order_id; }
                break;
            case EventType::TradeFill:
                if (payload.size() < sizeof(TradeFillEvent)) return;
                { TradeFillEvent e; std::memcpy(&e, payload.data(), sizeof(e)); id = e.your_order_id; }
                break;
            case EventType::Cancelled:
                if (payload.size() < sizeof(CancelEvent)) return;
                { CancelEvent e; std::memcpy(&e, payload.data(), sizeof(e)); id = e.order_id; }
                break;
            case EventType::BookUpdate:
                ++broadcasts;
                return;   // broadcast; not tied to any one sent command
            default:
                return;
        }

        if (!have_id) return;

        auto it = send_times.find(id);
        if (it != send_times.end())
        {
            auto rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_recv - it->second).count();
            hdr_record_value(histogram, rtt_ns);
            send_times.erase(it);
            responses.fetch_add(1, std::memory_order_relaxed);
        }
        // if not found: either a later event for an already-measured command, or a stale/unknown id
    });

    client.connect();
    std::println("waiting for handshake...");

    while (!connected.load())
    {
        client.tick();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // ---- pipeline send loop --------------------------------------------------

    std::println("sending workload...");
    auto start = std::chrono::steady_clock::now();

    size_t cmd_idx = 0;
    size_t sent    = 0;

    while (!disconnected.load() && (responses.load() < sent || cmd_idx < gen.commands().size()))
    {

        // keep PIPELINE commands outstanding
        while (sent - responses.load() < PIPELINE && cmd_idx < gen.commands().size())
        {

            const auto& cmd = gen.commands()[cmd_idx++];

            std::visit([&](auto& c)
            {
                using T = std::decay_t<decltype(c)>;

                if constexpr (std::is_same_v<T, NewOrderCmd>)
                {
                    // force GTC — FAK with no match generates no response, which would hang the client
                    NewOrderCmd outgoing = c;
                    outgoing.order_type  = OrderType::GTC;

                    send_times[outgoing.order_id] = std::chrono::steady_clock::now();
                    client.send_reliable(std::span<const std::byte>{
                        reinterpret_cast<const std::byte*>(&outgoing), sizeof(outgoing) });
                }
                else if constexpr (std::is_same_v<T, CancelOrderCmd>)
                {
                    send_times[c.order_id] = std::chrono::steady_clock::now();
                    client.send_reliable(std::span<const std::byte>{
                        reinterpret_cast<const std::byte*>(&c), sizeof(c) });
                }
            }, cmd);

            ++sent;

        }

        client.tick();

    }

    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::println("");
    std::println("--- full-system benchmark ---");
    std::println("commands sent:     {}", sent);
    std::println("responses matched: {}", responses.load());
    std::println("book broadcasts:   {}", broadcasts.load());
    std::println("total time:        {:.2f} ms", ms);
    std::println("throughput:        {:.2f} K ops/s", sent / (ms / 1000.0) / 1000.0);
    std::println("rtt p50:           {} ns", hdr_value_at_percentile(histogram, 50.0));
    std::println("rtt p99:           {} ns", hdr_value_at_percentile(histogram, 99.0));
    std::println("rtt p99.9:         {} ns", hdr_value_at_percentile(histogram, 99.9));
    std::println("rtt p99.99:        {} ns", hdr_value_at_percentile(histogram, 99.99));
    std::println("rtt max:           {} ns", hdr_max(histogram));

    FILE* out = std::fopen("system_latency.hgrm", "w");
    if (out)
    {
        hdr_percentiles_print(histogram, out, 5, 1.0, CLASSIC);
        std::fclose(out);
        std::println("wrote system_latency.hgrm");
    }

    hdr_close(histogram);
    return 0;

}
