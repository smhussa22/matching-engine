// engine-only benchmark for the matching engine (no network, no queue, no threads).
//
// three passes:
//   1. warmup           — primes caches and branch predictors, not measured.
//   2. throughput run   — total time over the whole workload, no per-op timing.
//                         gives the sustained ops/sec number.
//   3. latency run      — per-command chrono samples recorded into HdrHistogram.
//                         gives the latency distribution (p50/p99/p99.9/p99.99).
//
// headline numbers come from (2). (3) carries ~15-30ns of chrono::now overhead
// per sample, which is why we don't use (3)'s numbers for throughput.
//
// run: ./build/engine_bench
// optional: ./build/engine_bench <N>   where N is the number of commands

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <variant>
#include <vector>

#include <hdr/hdr_histogram.h>

#include "OrderBook.hh"
#include "OrderGenerator.hh"

struct EmptyListener
{
    void on_trade(uint64_t, uint64_t, int32_t, uint32_t, bool, bool) {}
    void on_order_added(uint64_t, int32_t, uint32_t, OrderDirection) {}
    void on_order_cancelled(uint64_t) {}
    void on_order_rejected(uint64_t, OrderRejected) {}
    void on_order_book_update(int32_t, uint32_t, OrderDirection) {}
};

template <typename Listener>
inline void dispatch(OrderBook<Listener>& book, const Command& cmd)
{
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
}

int main(int argc, char** argv)
{

    constexpr size_t   WARMUP_N = 100'000;
    constexpr size_t   RUNS     = 5;
    constexpr uint64_t SEED     = 42;

    size_t N = 10'000'000;
    if (argc > 1) N = static_cast<size_t>(std::atoll(argv[1]));

    std::println("pre-generating workload...");
    std::println("  warmup:   {} commands", WARMUP_N);
    std::println("  measured: {} commands", N);
    OrderGenerator warmup_gen { WARMUP_N, 1   };
    OrderGenerator bench_gen  { N,        SEED };

    std::println("workload mix: ~70% new orders / ~30% cancels, ~10% of new orders are FAK");
    std::println("");

    // ---- throughput: {RUNS} independent runs, report best --------------------

    std::println("=== throughput ({} runs, picks best) ===", RUNS);
    std::vector<double> throughputs;
    throughputs.reserve(RUNS);

    for (size_t run = 0; run < RUNS; ++run)
    {

        EmptyListener            listener;
        OrderBook<EmptyListener> book { listener };

        // warmup pass — not measured
        for (auto& cmd : warmup_gen.commands()) dispatch(book, cmd);

        auto t0 = std::chrono::steady_clock::now();
        for (auto& cmd : bench_gen.commands()) dispatch(book, cmd);
        auto t1 = std::chrono::steady_clock::now();

        double seconds    = std::chrono::duration<double>(t1 - t0).count();
        double throughput = static_cast<double>(N) / seconds;
        throughputs.push_back(throughput);

        std::println("  run {}: {:.2f} ms  ({:.2f} M ops/s)",
                     run + 1,
                     std::chrono::duration<double, std::milli>(t1 - t0).count(),
                     throughput / 1e6);

    }

    double best_throughput = *std::max_element(throughputs.begin(), throughputs.end());

    // ---- latency: separate run with per-command chrono samples ---------------

    std::println("");
    std::println("=== latency (HdrHistogram, {} samples) ===", N);

    hdr_histogram* histogram = nullptr;
    if (hdr_init(1, 1'000'000'000, 3, &histogram) != 0)
    {
        std::println("hdr_init failed");
        return 1;
    }

    EmptyListener            listener;
    OrderBook<EmptyListener> book { listener };
    for (auto& cmd : warmup_gen.commands()) dispatch(book, cmd);  // warmup

    for (auto& cmd : bench_gen.commands())
    {
        auto t0 = std::chrono::steady_clock::now();
        dispatch(book, cmd);
        auto t1 = std::chrono::steady_clock::now();
        hdr_record_value(histogram, std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::println("  p50:   {} ns", hdr_value_at_percentile(histogram, 50.0));
    std::println("  p99:   {} ns", hdr_value_at_percentile(histogram, 99.0));
    std::println("  p99.9: {} ns", hdr_value_at_percentile(histogram, 99.9));
    std::println("  p99.99:{} ns", hdr_value_at_percentile(histogram, 99.99));
    std::println("  max:   {} ns", hdr_max(histogram));
    std::println("  note:  per-sample chrono::now adds ~15-30 ns");

    FILE* out = std::fopen("engine_latency.hgrm", "w");
    if (out)
    {
        hdr_percentiles_print(histogram, out, 5, 1.0, CLASSIC);
        std::fclose(out);
    }

    // ---- headline ------------------------------------------------------------

    std::println("");
    std::println("================================================================");
    std::println(" headline: {:.2f} million orders/sec sustained throughput", best_throughput / 1e6);
    std::println("           p50 latency ~{} ns (incl. measurement overhead)", hdr_value_at_percentile(histogram, 50.0));
    std::println(" workload: {} commands, seeded, single-threaded, Release build", N);
    std::println(" plot:     upload engine_latency.hgrm to");
    std::println("           https://hdrhistogram.github.io/HdrHistogram/plotFiles.html");
    std::println("================================================================");

    hdr_close(histogram);
    return 0;

}
