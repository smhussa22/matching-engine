#include <maroof/net/Server.hh>
#include <maroof/net/Endpoint.hh>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <print>

int main()
{
    using namespace maroof::net;

    Server s { Endpoint("0.0.0.0", 9001) };

    s.on_connect([](Connection& c) 
    {
        std::cout << "[connect] " << c.peer().to_string() << '\n';
    });

    s.on_receive([](Connection& c, std::span<const std::byte> payload, std::uint8_t channel) 
    {
        std::cout << "[recv] " << payload.size() << " bytes on channel " << static_cast<int>(channel) << " from " << c.peer().to_string() << '\n';
    });

    s.on_disconnect([](const Endpoint& peer) 
    {
        std::cout << "[disconnect] " << peer.to_string() << '\n';
    });

    std::cout << "matcher listening on 0.0.0.0:9001\n";
    s.run();

    return 0;
    
}
