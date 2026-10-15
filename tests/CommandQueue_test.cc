#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "CommandQueue.hh"

static NewOrderCmd make_new_order(uint64_t id)
{
    NewOrderCmd cmd;
    cmd.order_id   = id;
    cmd.price      = 100;
    cmd.quantity   = 10;
    cmd.direction  = OrderDirection::Buy;
    cmd.order_type = OrderType::GTC;
    return cmd;
}

TEST(CommandQueueTest, PushAndPopRoundTrip)
{
    // a pushed command comes back out of pop with the same contents
    CommandQueue queue;
    queue.push(make_new_order(42));

    auto popped = queue.pop();
    ASSERT_TRUE(popped.has_value());
    ASSERT_TRUE(std::holds_alternative<NewOrderCmd>(*popped));
    EXPECT_EQ(std::get<NewOrderCmd>(*popped).order_id, 42u);
}

TEST(CommandQueueTest, MultiplePushesPreserveFifoOrder)
{
    // commands come out of pop in the order they went into push
    CommandQueue queue;
    queue.push(make_new_order(1));
    queue.push(make_new_order(2));
    queue.push(make_new_order(3));

    EXPECT_EQ(std::get<NewOrderCmd>(*queue.pop()).order_id, 1u);
    EXPECT_EQ(std::get<NewOrderCmd>(*queue.pop()).order_id, 2u);
    EXPECT_EQ(std::get<NewOrderCmd>(*queue.pop()).order_id, 3u);
}

TEST(CommandQueueTest, ShutDownWakesEmptyPopWithNullopt)
{
    // a thread blocked inside pop on an empty queue is woken by shut_down and returned nullopt
    CommandQueue queue;
    std::optional<CommandQueue::Command> result;

    std::thread consumer { [&] { result = queue.pop(); } };

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.shut_down();
    consumer.join();

    EXPECT_FALSE(result.has_value());
}

TEST(CommandQueueTest, ShutDownDrainsRemainingCommandsFirst)
{
    // after shut_down the queue still delivers already-pushed commands, then returns nullopt
    CommandQueue queue;
    queue.push(make_new_order(1));
    queue.push(make_new_order(2));
    queue.shut_down();

    auto first  = queue.pop();
    auto second = queue.pop();
    auto third  = queue.pop();

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<NewOrderCmd>(*first).order_id, 1u);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<NewOrderCmd>(*second).order_id, 2u);
    EXPECT_FALSE(third.has_value());
}

TEST(CommandQueueTest, PushWakesWaitingConsumer)
{
    // a thread blocked on an empty queue wakes up and receives the pushed command
    CommandQueue queue;
    std::optional<CommandQueue::Command> result;

    std::thread consumer { [&] { result = queue.pop(); } };

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.push(make_new_order(7));
    consumer.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<NewOrderCmd>(*result).order_id, 7u);
}

TEST(CommandQueueTest, ConcurrentProducersAllItemsReceived)
{
    // four producer threads push concurrently; the consumer thread eventually receives every command
    CommandQueue queue;
    constexpr int num_producers = 4;
    constexpr int per_producer  = 250;
    constexpr int total         = num_producers * per_producer;

    std::atomic<int> count { 0 };

    std::thread consumer { [&]
    {
        while (count.load() < total)
        {
            auto cmd = queue.pop();
            if (!cmd) return;
            ++count;
        }
    }};

    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    for (int t = 0; t < num_producers; ++t)
    {
        producers.emplace_back([&, t]
        {
            for (int i = 0; i < per_producer; ++i)
                queue.push(make_new_order(static_cast<uint64_t>(t * per_producer + i)));
        });
    }

    for (auto& p : producers) p.join();
    consumer.join();

    EXPECT_EQ(count.load(), total);
}
