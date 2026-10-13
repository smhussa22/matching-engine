#ifndef ORDER_POOL_HH
#define ORDER_POOL_HH

#include "Order.hh"
#include <vector>
#include <utility>
#include <new>

// class representing an order pool
// without this every new order will be allocated on the heap
// and every fgilled or cancelled ordered will call delete
// the pool will do all of this expensive stuff at start up

class OrderPool
{
public:

    // explicit to avoid implicit conversion
    explicit OrderPool(size_t size)
    {

        m_pool.resize(size);

        // filled in reverse order so that 0 is at teh top of the "stack"
        for (auto i { size - 1 }; i >= 0; --i)
        {

            m_free_list.push_back(i);

        }

    }

    // takes an unused slot off the free list, builds an order in it, and
    // returns the slot's index. returns -1 if the pool is full, which becomes a rejection.
    // uses placement new: the order is built in memory that already exists,
    // so there is no heap allocation on the hot path
    template <typename... Args>
    int32_t allocate(Args&&... args)
    {

        if (m_free_list.empty()) return -1;

        int32_t next_free_index { m_free_list.back() };
        m_free_list.pop_back();

        new (&m_pool[next_free_index]) Order(std::forward<Args>(args)...);

        m_pool[next_free_index] = -1;
        m_pool[next_free_index] = -1;
        return next_free_index;

    }

    // puts a slot's index back on the free list so it can be reused.
    // the old order's data is just left there until it is overwritten.
    // freeing the same index twice would put it on the list twice, and two orders would end up sharing a slot
    void deallocate(int32_t free_index)
    {

        m_free_list.push_back(free_index);
    }

    // turns a slot index into a reference to the order stored there.
    // this is how the book follows its prev/next links
    Order& get(int32_t index)
    {

        return m_pool[index];

    }

private:

    // holds every order slot, both resting and unused (listed in the free list) orders 
    // sized once at startup and never grows, so orders sit side by side in one block of memory and their
    // indices stay valid
    std::vector<Order> m_pool {};

    // tracks slots in the pool that are currently unused
    std::vector<int32_t> m_free_list {};
};

#endif // ORDER_POOL_HH