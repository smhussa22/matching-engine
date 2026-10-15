#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <maroof/net/Endpoint.hh>
#include "ExchangeServer.hh"

TEST(ExchangeServerTest, StopIsSafeBeforeStart)
{
    // stop before start is a no-op and must not crash
    ExchangeServer server { maroof::net::Endpoint { "127.0.0.1", 59876 } };
    server.stop();
}

TEST(ExchangeServerTest, DestructorCleansUpWithoutStart)
{
    // destruction without ever calling start is safe
    ExchangeServer server { maroof::net::Endpoint { "127.0.0.1", 59877 } };
}

TEST(ExchangeServerTest, StartStopLifecycle)
{
    // start runs in a background thread; stop from the main thread exits the server loop and joins cleanly
    ExchangeServer server { maroof::net::Endpoint { "127.0.0.1", 59878 } };

    std::thread server_thread { [&] { server.start(); } };
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();
    server_thread.join();
}

TEST(ExchangeServerTest, StopIsIdempotent)
{
    // calling stop more than once after start is safe
    ExchangeServer server { maroof::net::Endpoint { "127.0.0.1", 59879 } };

    std::thread server_thread { [&] { server.start(); } };
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();
    server.stop();
    server.stop();
    server_thread.join();
}
