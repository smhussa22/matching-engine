// engine-only benchmark: measures OrderBook in isolation.
// no network, no queue, no threads. just: generator -> book -> empty listener.
// the gap between this number and the full-system benchmark tells us what the
// network and threading layer actually cost.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <print>
#include <variant>
#include <vector>

#include "OrderBook.hh"
#include "OrderGenerator.hh"

// no-op listener so callback cost does not pollute the measurement
struct EmptyListener
{
    void on_trade(uint64_t, uint64_t, int32_t, uint32_t, bool, bool) {}
    void on_order_added(uint64_t, int32_t, uint32_t, OrderDirection) {}
    void on_order_cancelled(uint64_t) {}
    void on_order_rejected(uint64_t, OrderRejected) {}
    void on_order_book_update(int32_t, uint32_t, OrderDirection) {}
};

// pulls out a given percentile from a sorted latency vector
static uint64_t percentile(const std::vector<uint64_t>& sorted, double p)
{
    size_t index = static_cast<size_t>(sorted.size() * p);
    if (index >= sorted.size()) index = sorted.size() - 1;
    return sorted[index];
}

int main()
{

    constexpr size_t   N    = 1'000'000;
    constexpr uint64_t SEED = 42;

    std::println("pre-generating {} commands...", N);
    OrderGenerator gen { N, SEED };

    EmptyListener            listener;
    OrderBook<EmptyListener> book { listener };

    // per-command latency samples; preallocated so push_back never reallocates during the hot loop
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(N);

    std::println("running benchmark...");

    auto total_start = std::chrono::steady_clock::now();

    for (auto& cmd : gen.commands())
    {

        auto t0 = std::chrono::steady_clock::now();

        std::visit([&](auto& c)
        {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, NewOrderCmd>)
            {
                Order order { c.order_id, c.price, static_cast<int32_t>(c.quantity), c.direction };
                book.submit_order(order, c.order_type);
            }
            else if constexpr (std::is_same_v<T, CancelOrderCmd>)
            {
                book.cancel_order(c.order_id);
            }
        }, cmd);

        auto t1 = std::chrono::steady_clock::now();
        latencies_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    }

    auto total_end = std::chrono::steady_clock::now();
    uint64_t total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start).count();

    std::sort(latencies_ns.begin(), latencies_ns.end());

    double seconds    = total_ns / 1e9;
    double throughput = static_cast<double>(N) / seconds;

    std::println("");
    std::println("--- engine-only benchmark ---");
    std::println("commands:     {}",            N);
    std::println("total time:   {:.3f} ms",     total_ns / 1e6);
    std::println("throughput:   {:.2f} ops/s",  throughput);
    std::println("latency p50:  {} ns",         percentile(latencies_ns, 0.50));
    std::println("latency p99:  {} ns",         percentile(latencies_ns, 0.99));
    std::println("latency p99.9:{} ns",         percentile(latencies_ns, 0.999));
    std::println("latency max:  {} ns",         latencies_ns.back());
    std::println("");
    std::println("note: measurement overhead (~15-30ns from chrono::now) is included in each sample");

    return 0;

}
