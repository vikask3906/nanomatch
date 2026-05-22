#pragma once
#include "spsc_queue.hpp"
#include <fstream>
#include <thread>
#include <atomic>
#include <cstdio>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

class TradeLogger {
public:
    explicit TradeLogger(TradeQueue& q, const char* path, int cpu_core = 1)
        : queue_(q), file_(path), running_(true)
    {
        worker_ = std::thread([this, cpu_core] { run(cpu_core); });
    }

    ~TradeLogger() {
        running_.store(false, std::memory_order_release);
        worker_.join();
    }

    uint64_t trades_logged() const {
        return logged_.load(std::memory_order_relaxed);
    }

private:
    void run(int core) {
#ifdef __linux__
        // Pin logger to a dedicated core — prevents OS migration blowing cache
        // Only attempt if core is valid
        if (core >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
        }
#endif
        // Write CSV header
        file_ << "buy_id,sell_id,price,qty,timestamp\n";

        while (running_.load(std::memory_order_acquire)) {
            auto trade = queue_.pop();
            if (trade) {
                file_ << trade->buy_order_id  << ','
                      << trade->sell_order_id << ','
                      << trade->price         << ','
                      << trade->quantity      << ','
                      << trade->timestamp     << '\n';
                logged_.fetch_add(1, std::memory_order_relaxed);
            }
            // Busy-spin — no sleep, no condition_variable, no kernel call
            // __builtin_ia32_pause() reduces power consumption during spin
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }

        // Drain remaining trades after stop signal
        while (true) {
            auto trade = queue_.pop();
            if (!trade) break;
            file_ << trade->buy_order_id  << ','
                  << trade->sell_order_id << ','
                  << trade->price         << ','
                  << trade->quantity      << ','
                  << trade->timestamp     << '\n';
            logged_.fetch_add(1, std::memory_order_relaxed);
        }

        file_.flush();
    }

    TradeQueue&          queue_;
    std::ofstream        file_;
    std::atomic<bool>    running_;
    std::atomic<uint64_t> logged_{0};
    std::thread          worker_;
};
