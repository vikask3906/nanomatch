#pragma once
#include "order.hpp"

struct PriceLevel {
    uint64_t price         = 0;
    uint64_t total_qty     = 0;
    uint32_t order_count   = 0;
    Order*   head          = nullptr;
    Order*   tail          = nullptr;

    void push(Order* o) {
        o->next = nullptr;
        if (!tail) { head = tail = o; }
        else       { tail->next = o; tail = o; }
        total_qty += o->quantity;
        ++order_count;
    }

    Order* front() const { return head; }

    // Pop head — does NOT touch total_qty (caller manages it via fill_qty/reduce_qty)
    Order* pop() {
        if (!head) return nullptr;
        Order* o = head;
        --order_count;
        head = head->next;
        if (!head) tail = nullptr;
        return o;
    }

    bool empty() const { return head == nullptr; }

    // Decrement total_qty by the amount just filled or cancelled
    void fill_qty(uint64_t qty) {
        total_qty = (qty >= total_qty) ? 0 : total_qty - qty;
    }

    void reduce_qty(uint64_t qty) { fill_qty(qty); }  // alias for cancel
};
