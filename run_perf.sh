#!/bin/bash
# NanoMatch — Linux Profiling Script
# Run this on your Linux machine (or WSL2 if hardware counters are enabled)

set -e

echo "── NanoMatch Profiling Script ──"

# 1. Build
echo "Building Linux binaries..."
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_simple.cpp -o nanomatch_opt
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp bench/bench_standalone.cpp -o bench_standalone

# Ensure data exists
if [ ! -f "data/orders_500k.csv" ]; then
    echo "Generating 500k dataset..."
    python3 data/gen_orders.py -n 500000 -o data/orders_500k.csv
fi

echo ""
echo "── 1. Cache Miss Analysis (perf stat) ──"
echo "Running optimized engine to capture cache misses..."
perf stat -e cache-misses,cache-references,L1-dcache-load-misses,instructions,cycles \
    ./nanomatch_opt data/orders_500k.csv

echo ""
echo "── 2. Flame Graph Generation (perf record) ──"
echo "Recording CPU profile..."
perf record -g --call-graph dwarf ./nanomatch_opt data/orders_500k.csv

echo "Generating flame graph..."
# Note: Requires Brendan Gregg's FlameGraph scripts in your PATH
if command -v stackcollapse-perf.pl &> /dev/null; then
    perf script | stackcollapse-perf.pl | flamegraph.pl > flame_optimized.svg
    echo "Saved to flame_optimized.svg"
else
    echo "WARN: stackcollapse-perf.pl not found. You need Brendan Gregg's FlameGraph tools."
    echo "Clone them via: git clone https://github.com/brendangregg/FlameGraph"
    echo "Then run manually: perf script | ./FlameGraph/stackcollapse-perf.pl | ./FlameGraph/flamegraph.pl > flame_optimized.svg"
fi

echo ""
echo "Profiling complete. Copy flame_optimized.svg to your report!"
