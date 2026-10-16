// a small end-to-end demo of the matching engine.
// prints each callback as it happens so you can see orders flowing through the book,
// then runs a short seeded workload to show the engine handling many orders at once.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <print>

#include "OrderBook.hh"
#include "OrderGenerator.hh"

// listener that prints every engine event so you can watch the demo
struct ConsoleListener
{
    void on_trade(uint64_t agg, uint64_t pass, int32_t price, uint32_t qty,
                  bool agg_filled, bool pass_filled)
    {
        std::println("  trade: {} @ {} (aggressor={} filled={}, passive={} filled={})",
                     qty, price, agg, agg_filled, pass, pass_filled);
    }

    void on_order_added(uint64_t id, int32_t price, uint32_t qty, OrderDirection direction)
    {
        std::println("  rested: id={} {} {} @ {}", id, direction, qty, price);
    }

    void on_order_cancelled(uint64_t id)
    {
        std::println("  cancelled: id={}", id);
    }

    void on_order_rejected(uint64_t id, OrderRejected reason)
    {
        std::println("  rejected: id={} ({})", id, reason);
    }

    void on_order_book_update(int32_t, uint32_t, OrderDirection) {}
};

// listener that does nothing, used for the volume demo so the output stays short
struct SilentListener
{
    void on_trade(uint64_t, uint64_t, int32_t, uint32_t, bool, bool) {}
    void on_order_added(uint64_t, int32_t, uint32_t, OrderDirection) {}
    void on_order_cancelled(uint64_t) {}
    void on_order_rejected(uint64_t, OrderRejected) {}
    void on_order_book_update(int32_t, uint32_t, OrderDirection) {}
};

static void guided_demo()
{

    std::println("=== guided demo (verbose) ===\n");

    ConsoleListener listener;
    OrderBook<ConsoleListener> book { listener };

    std::println("submitting sell 10 @ 100 (id=1)");
    book.submit_order(Order { 1, 100, 10, OrderDirection::Sell }, OrderType::GTC);

    std::println("\nsubmitting sell 10 @ 101 (id=2)");
    book.submit_order(Order { 2, 101, 10, OrderDirection::Sell }, OrderType::GTC);

    std::println("\nsubmitting buy 15 @ 102 (id=3) -- should sweep both levels and rest the leftover");
    book.submit_order(Order { 3, 102, 15, OrderDirection::Buy }, OrderType::GTC);

    std::println("\nsubmitting FAK buy 100 @ 200 (id=4) -- nothing to match, remainder discarded");
    book.submit_order(Order { 4, 200, 100, OrderDirection::Buy }, OrderType::FAK);

    std::println("\ncancelling id=3");
    book.cancel_order(3);

    std::println("\ncancelling id=999 (does not exist)");
    book.cancel_order(999);

    std::println("\nfinal book:");
    book.print(std::cout);

}

static void volume_demo()
{

    std::println("\n=== volume demo (silent) ===\n");

    constexpr size_t   N    = 100'000;
    constexpr uint64_t SEED = 42;

    std::println("pre-generating {} commands with seed {}...", N, SEED);
    OrderGenerator gen { N, SEED };

    SilentListener            listener;
    OrderBook<SilentListener> book { listener };

    std::println("feeding commands into the engine...");

    auto start = std::chrono::steady_clock::now();

    for (auto& cmd : gen.commands())
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

    auto end = std::chrono::steady_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::println("processed {} commands in {} ms", N, ms);
    std::println("(for proper latency numbers run ./build/engine_bench)");

}

int main()
{

    guided_demo();
    volume_demo();
    return 0;

}
