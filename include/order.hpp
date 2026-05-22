#pragma once
#include <cstdint>

enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1, CANCEL = 2 };

struct Order {
    uint64_t  order_id;
    uint64_t  price;       // integer ticks, no floats
    uint64_t  quantity;    // remaining quantity
    uint64_t  timestamp;   // rdtsc value at arrival
    Side      side;
    OrderType type;
    bool      cancelled;   // lazy deletion flag
    uint8_t   _pad[5];     // explicit padding
    Order*    next;        // intrusive linked list

    Order() : order_id(0), price(0), quantity(0), timestamp(0),
              side(Side::BUY), type(OrderType::LIMIT),
              cancelled(false), next(nullptr) {}
};

static_assert(sizeof(Order) == 48, "Order size mismatch");

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
};
