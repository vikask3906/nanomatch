#pragma once
#include <atomic>
#include <optional>
#include <cstdint>
#include "order.hpp"

// Single Producer Single Consumer ring buffer
// Producer = matching engine thread
// Consumer = logger thread
// Zero mutex, zero syscall in hot path
// N must be power of 2

template<typename T, std::size_t N>
class SPSCQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static constexpr uint64_t MASK = N - 1;

    // head and tail on SEPARATE cache lines — prevents false sharing
    // Without this: writing head invalidates tail's cache line on other core
    struct alignas(64) AlignedAtomic {
        std::atomic<uint64_t> val{0};
    };

    AlignedAtomic  head_;             // written by producer, read by consumer
    AlignedAtomic  tail_;             // written by consumer, read by producer
    alignas(64) T  buffer_[N];        // ring buffer storage

public:
    // Called ONLY by producer thread
    bool push(const T& item) {
        const uint64_t h = head_.val.load(std::memory_order_relaxed);
        const uint64_t next_h = (h + 1) & MASK;

        // Full check — acquire ensures we see consumer's latest tail update
        if (__builtin_expect(next_h == tail_.val.load(std::memory_order_acquire), 0))
            return false;  // queue full

        buffer_[h] = item;

        // Release: guarantees buffer_[h] write is visible before head update
        head_.val.store(next_h, std::memory_order_release);
        return true;
    }

    // Called ONLY by consumer thread
    std::optional<T> pop() {
        const uint64_t t = tail_.val.load(std::memory_order_relaxed);

        // Empty check — acquire ensures we see producer's latest head update
        if (t == head_.val.load(std::memory_order_acquire))
            return std::nullopt;

        T item = buffer_[t];

        // Release: signals producer that slot t is now free
        tail_.val.store((t + 1) & MASK, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return tail_.val.load(std::memory_order_acquire) ==
               head_.val.load(std::memory_order_acquire);
    }

    std::size_t size_approx() const {
        uint64_t h = head_.val.load(std::memory_order_relaxed);
        uint64_t t = tail_.val.load(std::memory_order_relaxed);
        return (h - t) & MASK;
    }
};

// 1M trade slots — each Trade is ~40 bytes → ~40MB ring buffer
using TradeQueue = SPSCQueue<Trade, 1 << 20>;
