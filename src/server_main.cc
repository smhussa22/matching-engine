// server runner. constructs an ExchangeServer, binds it to a port, and blocks
// inside start() until Ctrl+C triggers a clean shutdown.

#include <atomic>
#include <csignal>
#include <print>

#include <maroof/net/Endpoint.hh>
#include "ExchangeServer.hh"

static std::atomic<ExchangeServer*> g_server { nullptr };

static void on_signal(int)
{
    auto* s = g_server.load();
    if (s) s->stop();
}

int main(int argc, char** argv)
{

    uint16_t port = 9000;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    maroof::net::Endpoint bind_addr { "0.0.0.0", port };
    ExchangeServer        server   { bind_addr };

    g_server.store(&server);
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::println("exchange server listening on 0.0.0.0:{}", port);
    std::println("press Ctrl+C to stop");

    server.start();   // blocks until on_signal calls stop()

    std::println("server stopped cleanly");
    return 0;

}
