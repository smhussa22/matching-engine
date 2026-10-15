#ifndef ORDER_GENERATOR_HH
#define ORDER_GENERATOR_HH

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "Protocol.hh"

// pre-generates a deterministic stream of client commands so benchmarks can replay
// the same workload every run. generation happens once up front; the benchmark loop
// just reads from the vector so timing does not include generation cost.
class OrderGenerator
{
public:

    // count = how many commands to produce in total
    // seed = fixed seed for the rng so runs are reproducible
    // mid_price= price the generator drifts around when choosing new order prices
    OrderGenerator(size_t count, uint64_t seed, int32_t mid_price = 1000);

    ~OrderGenerator() = default;

    OrderGenerator(const OrderGenerator&) = delete;
    OrderGenerator& operator=(const OrderGenerator&) = delete;
    OrderGenerator(OrderGenerator&&) = delete;
    OrderGenerator& operator=(OrderGenerator&&) = delete;

    // the pre-generated workload. iterates this in the benchmark hot loop.
    const std::vector<Command>& commands() const noexcept { return m_commands; }

    size_t size() const noexcept { return m_commands.size(); }

private:

    // roughly how often each command type is produced; adjust if you want a different mix
    static constexpr double NEW_ORDER_PROBABILITY { 0.70 };
    // the other 0.30 are cancels. no market/FAK yet.

    // builds a new order with a random side, size, and price drifted around the current mid
    NewOrderCmd make_new_order(uint64_t id);

    // picks a previously-generated resting order id to cancel.
    // returns std::nullopt if there is nothing to cancel yet (very early in generation).
    std::optional<CancelOrderCmd> make_cancel();

    std::vector<Command> m_commands;

    std::mt19937_64 m_rng;

    // the mid price drifts slightly over the run so the book doesn't fill up at one tick
    int32_t m_current_mid;

    // ids we've handed out as new orders, so cancels can target real ids.
    // we don't remove ids on cancel generation; a double-cancel will just be rejected at run time,
    // which is a realistic workload shape anyway.
    std::vector<uint64_t> m_live_ids;

    // next id to assign to a new order
    uint64_t m_next_id { 1 };

};

inline OrderGenerator::OrderGenerator(size_t count, uint64_t seed, int32_t mid_price) : m_rng { seed } , m_current_mid { mid_price }
{

    m_commands.reserve(count);
    m_live_ids.reserve(count);

    std::uniform_real_distribution<double> type_dist { 0.0, 1.0 };

    for (size_t i = 0; i < count; ++i)
    {

        bool make_new = type_dist(m_rng) < NEW_ORDER_PROBABILITY || m_live_ids.empty();

        if (make_new)
        {

            m_commands.push_back(make_new_order(m_next_id++));

        }
        else
        {

            auto cancel = make_cancel();
            if (cancel) m_commands.push_back(*cancel);
            else m_commands.push_back(make_new_order(m_next_id++));

        }

    }

}

inline NewOrderCmd OrderGenerator::make_new_order(uint64_t id)
{

    // drift the mid slightly on each call so the book spreads across many price levels
    std::uniform_int_distribution<int32_t> drift { -1, 1 };
    m_current_mid += drift(m_rng);

    // price offset from the mid; small range keeps most orders near the top of book
    std::uniform_int_distribution<int32_t> price_offset { -10, 10 };
    std::uniform_int_distribution<uint32_t> qty { 1, 100 };
    std::uniform_int_distribution<int> side_coin { 0, 1 };
    std::uniform_int_distribution<int> type_coin { 0, 9 };

    NewOrderCmd cmd;
    cmd.order_id = id;
    cmd.price = m_current_mid + price_offset(m_rng);
    cmd.quantity = qty(m_rng);
    cmd.direction = (side_coin(m_rng) == 0) ? OrderDirection::Buy : OrderDirection::Sell;
    cmd.order_type = (type_coin(m_rng) == 0) ? OrderType::FAK : OrderType::GTC;  // ~10% FAK

    // remember GTC ids so cancels can target them; FAK never rests so there's nothing to cancel
    if (cmd.order_type == OrderType::GTC)
        m_live_ids.push_back(id);

    return cmd;

}

inline std::optional<CancelOrderCmd> OrderGenerator::make_cancel()
{

    if (m_live_ids.empty()) return std::nullopt;

    std::uniform_int_distribution<size_t> pick { 0, m_live_ids.size() - 1 };
    CancelOrderCmd cmd;
    cmd.order_id = m_live_ids[pick(m_rng)];
    return cmd;

}

#endif // ORDER_GENERATOR_HH
