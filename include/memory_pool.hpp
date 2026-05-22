#pragma once
#include "order.hpp"
#include <cstdint>
#include <cassert>
#include <cstring>

template<std::size_t N>
class OrderPool {
public:
    OrderPool() {
        for (uint32_t i = 0; i < N; ++i)
            free_stack[i] = i;
        free_top = static_cast<uint32_t>(N);
    }

    Order* alloc() {
        assert(free_top > 0 && "OrderPool exhausted");
        Order* o = &storage[free_stack[--free_top]];
        // reset fields without calling constructor (we own the memory)
        o->order_id  = 0;
        o->price     = 0;
        o->quantity  = 0;
        o->timestamp = 0;
        o->cancelled = false;
        o->next      = nullptr;
        return o;
    }

    void dealloc(Order* o) {
        uint32_t idx = static_cast<uint32_t>(o - storage);
        assert(idx < N && "Pointer not from this pool");
        free_stack[free_top++] = idx;
    }

    // Direct index access for cancel lookups
    Order* get(uint32_t idx) {
        assert(idx < N);
        return &storage[idx];
    }

    uint32_t index_of(const Order* o) const {
        return static_cast<uint32_t>(o - storage);
    }

    std::size_t capacity() const { return N; }
    std::size_t available() const { return free_top; }

private:
    alignas(64) Order    storage[N];
    uint32_t             free_stack[N];
    uint32_t             free_top;
};

// Default pool size: 1 million live orders
static constexpr std::size_t POOL_SIZE = 1'000'000;
using EnginePool = OrderPool<POOL_SIZE>;
