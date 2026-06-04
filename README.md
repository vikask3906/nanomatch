# NanoMatch — Ultra-Low Latency Order Matching Engine

> **FEC · IIT Guwahati · DIY '26 · PS 03/06 · Quant - Systems**

A production-grade **Limit Order Book (LOB)** matching engine built from scratch in C++17, engineered with strict hardware sympathy principles. NanoMatch strips away OS-level overhead and STL abstractions to achieve **sub-microsecond deterministic matching** — the same class of techniques used by real-world HFT firms.

The project ships with a **real-time web visualizer** (Node.js + WebSocket) that live-streams the C++ engine's order book state to a browser dashboard, so you can *watch* the matching happen at human-readable speed.

---

## Table of Contents

- [Quick Demo](#-quick-demo)
- [Prerequisites](#-prerequisites)
- [Setup on a New Machine](#-setup-on-a-new-machine)
  - [Step 1 — Clone the Repository](#step-1--clone-the-repository)
  - [Step 2 — Generate Test Data](#step-2--generate-test-data)
  - [Step 3 — Build the Engine](#step-3--build-the-engine)
  - [Step 4 — Run the Tests](#step-4--run-the-tests)
  - [Step 5 — Run the Benchmark](#step-5--run-the-benchmark)
  - [Step 6 — Launch the Live Visualizer](#step-6--launch-the-live-visualizer-demo)
- [Directory Structure](#-directory-structure)
- [How the Demo Works](#-how-the-demo-works)
- [Architecture Decisions](#-architecture-decisions)
- [Performance Results](#-performance-results)
- [Test Suite](#-test-suite--1818-passing-)
- [Linux Profiling](#-linux-profiling-flame-graphs--cache-analysis)
- [Troubleshooting](#-troubleshooting)

---

## 🚀 Quick Demo

If you just want to see the visualizer running as fast as possible:

```bash
# 1. Build the engine
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_demo.cpp -o nanomatch_demo

# 2. Install demo dependencies & launch
cd demo && npm install && npm start
```

Open **http://localhost:3000** — you'll see a live order book with depth bars, a trade tape, and real-time metrics streaming directly from the C++ engine.

---

## 📋 Prerequisites

You need the following tools installed on your machine. The engine itself has **zero external C++ dependencies** — no Boost, no vcpkg, no Conan.

| Tool | Version | Purpose | Install |
|---|---|---|---|
| **C++ Compiler** | C++17 support | Build the matching engine | `g++` (Linux/WSL), MSVC (Windows), `clang++` (macOS) |
| **Python 3** | 3.7+ | Generate synthetic test data | [python.org](https://www.python.org/) |
| **Node.js** | 16+ | Run the live demo server | [nodejs.org](https://nodejs.org/) |
| **CMake** *(optional)* | 3.16+ | Alternate build system | [cmake.org](https://cmake.org/) |
| **Git** | any | Clone the repository | [git-scm.com](https://git-scm.com/) |

### Platform Support

| Platform | Compiler | Status |
|---|---|---|
| **Windows** (native) | MSVC 2019+, MinGW g++ 10+ | ✅ Fully tested |
| **Linux** | GCC 10+, Clang 12+ | ✅ Fully tested |
| **macOS** | Apple Clang 14+ | ✅ Should work (untested) |
| **WSL2** | GCC 10+ | ✅ Recommended for `perf` profiling |

---

## 🔧 Setup on a New Machine

Follow these steps in order. The entire setup takes **under 3 minutes**.

### Step 1 — Clone the Repository

```bash
git clone https://github.com/vikask3906/nanomatch.git
cd nanomatch
```

### Step 2 — Generate Test Data

The engine processes synthetic order flow from CSV or binary ITCH files. Generate them with the included Python scripts:

```bash
# CSV dataset (500K orders — ~30MB)
python3 data/gen_orders.py -n 500000 -o data/orders_500k.csv

# Binary ITCH 5.0 dataset (500K orders — ~17MB, for zero-copy mmap benchmark)
python3 data/gen_itch.py -n 500000 -o data/orders_500k.itch
```

> **Note:** A small 10K-order CSV (`data/small.csv`) is already included in the repo for quick smoke tests.

### Step 3 — Build the Engine

Pick your platform:

<details>
<summary><strong>🐧 Linux / WSL / macOS — GCC/Clang (recommended)</strong></summary>

**Direct compilation (fastest, no CMake needed):**

```bash
# Single-threaded engine
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_simple.cpp \
    -o nanomatch_simple

# Threaded engine (matching thread + SPSC logger thread)
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main.cpp \
    -lpthread -o nanomatch

# ITCH binary parser engine
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_itch.cpp \
    -o nanomatch_itch

# Demo binary (JSON stdout for web visualizer)
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_demo.cpp \
    -lpthread -o nanomatch_demo

# Standalone benchmark (optimized vs naive comparison)
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp bench/bench_standalone.cpp \
    -o bench_standalone

# Test suite (18 tests, no external dependencies; -lpthread for SPSC concurrency tests)
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp \
    tests/test_matching_standalone.cpp -lpthread -o run_tests
```

**CMake build (builds all targets at once):**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Binaries are in the build directory:
# ./nanomatch_simple, ./nanomatch, ./nanomatch_itch, ./nanomatch_demo,
# ./bench_standalone, ./tests_standalone
```

</details>

<details>
<summary><strong>🪟 Windows — MSVC (Visual Studio)</strong></summary>

**Option A: Developer Command Prompt (cl.exe)**

```cmd
cl /std:c++17 /O2 /EHsc /Iinclude src\order_book.cpp src\main_simple.cpp /Fe:nanomatch_simple.exe
cl /std:c++17 /O2 /EHsc /Iinclude src\order_book.cpp src\main_demo.cpp /Fe:nanomatch_demo.exe
cl /std:c++17 /O2 /EHsc /Iinclude src\order_book.cpp bench\bench_standalone.cpp /Fe:bench_standalone.exe
cl /std:c++17 /Od /Zi /EHsc /Iinclude src\order_book.cpp tests\test_matching_standalone.cpp /Fe:run_tests.exe
```

**Option B: CMake + Visual Studio Generator**

```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release

:: Binaries land in build\Release\
Release\nanomatch_simple.exe ..\data\small.csv
Release\tests_standalone.exe
Release\bench_standalone.exe ..\data\orders_500k.csv
```

**Option C: MinGW / MSYS2 g++**

Same as the Linux commands above — just use `g++` from your MSYS2/MinGW terminal and change the output extension to `.exe`:

```bash
g++ -std=c++17 -O3 -Iinclude src/order_book.cpp src/main_demo.cpp -o nanomatch_demo.exe
```

</details>

### Step 4 — Run the Tests

```bash
./run_tests        # Linux/macOS
run_tests.exe      # Windows
```

Expected output:

```
── NanoMatch Test Suite ────────────────────────────
  FullFill                                     PASS
  PartialFill_BuyLarger                        PASS
  ...
  OutOfRangePrice_Ignored                      PASS
  SPSC_SingleThread_FIFO                       PASS
  SPSC_FullQueue_Backpressure                  PASS
  SPSC_ConcurrentProducerConsumer              PASS
────────────────────────────────────────────────────
Results: 18 passed, 0 failed
```

### Step 5 — Run the Benchmark

```bash
./bench_standalone data/orders_500k.csv        # Linux/macOS
bench_standalone.exe data\orders_500k.csv      # Windows
```

This runs the **optimized engine** (memory pool + flat array) and the **naive STL baseline** (`std::map` + `new/delete`) side-by-side on the same dataset, then prints a comparison table proving the architecture decisions produce real performance gains.

### Step 6 — Launch the Live Visualizer (Demo)

The visualizer is a Node.js server that spawns the C++ `nanomatch_demo` binary, reads its JSON output from stdout, and pushes it to connected browser clients via WebSocket.

```bash
# 1. Make sure nanomatch_demo is built in the project root
#    (see Step 3 — the binary must be at ./nanomatch_demo or ./nanomatch_demo.exe)

# 2. Install Node.js dependencies (one time only)
cd demo
npm install

# 3. Start the server
npm start

# Or with custom speed (orders/sec) and snapshot frequency:
node server.js 120 5    # 120 orders/sec, snapshot every 5 orders
```

Open your browser to **http://localhost:3000** and watch the matching engine in real-time.

To stop the demo, press `Ctrl+C` in the terminal.

---

## 📁 Directory Structure

```
nanomatch/
├── CMakeLists.txt                  # Cross-platform build system
├── README.md                       # ← You are here
│
├── include/                        # All headers (engine is header+src split)
│   ├── order.hpp                   # Order, Trade, Side, OrderType structs
│   ├── price_level.hpp             # Intrusive FIFO queue per price point
│   ├── memory_pool.hpp             # Pre-allocated flat array, O(1) alloc
│   ├── spsc_queue.hpp              # Lock-free SPSC ring buffer (acquire/release)
│   ├── order_book.hpp              # Optimized OrderBook class declaration
│   ├── order_book_naive.hpp        # Naive STL baseline (std::map + new/delete)
│   ├── csv_parser.hpp              # Fast byte-scan CSV parser + replayer
│   ├── itch_parser.hpp             # Zero-copy mmap ITCH 5.0 binary parser
│   └── logger.hpp                  # Logger thread consuming SPSC trade queue
│
├── src/                            # Source files
│   ├── order_book.cpp              # Full matching logic implementation
│   ├── main.cpp                    # Threaded entry point (engine + logger)
│   ├── main_simple.cpp             # Single-threaded entry point
│   ├── main_itch.cpp               # ITCH binary ingestion entry point
│   └── main_demo.cpp               # JSON-streaming demo for web visualizer
│
├── bench/
│   ├── bench_standalone.cpp        # Optimized vs Naive comparison (no deps)
│   └── bench_engine.cpp            # Google Benchmark version (optional)
│
├── tests/
│   ├── test_matching_standalone.cpp  # 18 tests (incl. SPSC concurrency), no external deps
│   └── test_matching.cpp             # GTest version (optional)
│
├── data/
│   ├── gen_orders.py               # Synthetic CSV order generator
│   ├── gen_itch.py                  # Binary ITCH 5.0 data generator
│   ├── small.csv                   # 10K orders (test dataset, committed)
│   ├── orders_500k.csv             # 500K orders (generated, gitignored)
│   └── orders_500k.itch            # 500K binary ITCH (generated, gitignored)
│
├── demo/                           # Live Order Book Visualizer (Web UI)
│   ├── server.js                   # Node.js + Express + WebSocket server
│   ├── package.json                # Node dependencies (express, ws)
│   └── public/
│       ├── index.html              # Dashboard HTML
│       ├── style.css               # Glassmorphism dark theme
│       └── app.js                  # WebSocket client + DOM rendering
│
└── run_perf.sh                     # Linux perf profiling script (flame graphs)
```

---

## 🖥️ How the Demo Works

The live visualizer is a **three-layer pipeline** that bridges the C++ engine to your browser:

```
┌──────────────────────────────────────────────────────────────────────┐
│  LAYER 1: C++ Engine (nanomatch_demo)                                │
│  ─────────────────────────────────────                               │
│  • Loads 500K synthetic orders from CSV                              │
│  • Replays them through the matching engine at a throttled speed     │
│    (default: 80 orders/sec for visual clarity)                       │
│  • Every N orders, serializes the full book state to JSON:           │
│    { bids: [...], asks: [...], last_trade: {...}, stats: {...} }     │
│  • Writes JSON lines to stdout (one JSON object per line)            │
└────────────────────────────┬─────────────────────────────────────────┘
                             │ stdout (pipe)
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│  LAYER 2: Node.js Bridge Server (demo/server.js)                     │
│  ───────────────────────────────────────────                         │
│  • Spawns nanomatch_demo as a child process                          │
│  • Reads JSON lines from its stdout pipe                             │
│  • Serves the static HTML/CSS/JS frontend on HTTP (port 3000)        │
│  • Upgrades client connections to WebSocket                          │
│  • Broadcasts each JSON snapshot to all connected browsers           │
│  • Auto-restarts the engine when replay completes                    │
└────────────────────────────┬─────────────────────────────────────────┘
                             │ WebSocket (ws://localhost:3000)
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│  LAYER 3: Browser Dashboard (demo/public/*)                          │
│  ──────────────────────────────────────────                          │
│  • Connects via WebSocket, auto-reconnects on disconnect             │
│  • Renders the ORDER BOOK: top 10 bid/ask levels with depth bars     │
│  • Renders the TRADE TAPE: scrolling feed of matched executions      │
│  • Live metrics: orders processed, trades matched, throughput,       │
│    bid-ask spread                                                    │
│  • Flash animations on quantity changes, slide-in for new trades     │
│  • Connection status indicator (green/yellow/red dot)                │
└──────────────────────────────────────────────────────────────────────┘
```

### What You See in the Browser

| Panel | What It Shows |
|---|---|
| **Header Metrics** | Live counters: Orders Processed, Trades Matched, Throughput (ord/sec), Bid-Ask Spread |
| **Order Book** | Top 10 ask levels (red, sorted highest→lowest) and top 10 bid levels (green, sorted highest→lowest), each with depth bars showing relative quantity |
| **Spread Row** | Best Bid price, Best Ask price, and the spread between them |
| **Trade Tape** | Scrolling feed of the most recent trade executions, color-coded BUY (green) / SELL (red) with slide-in animation |
| **Status Bar** | Engine connection status, technology stack footer |

### Controlling the Demo Speed

```bash
# Default: 80 orders/sec, snapshot every 5 orders
node server.js

# Faster replay: 200 orders/sec, snapshot every 3 orders
node server.js 200 3

# Slow-motion: 20 orders/sec, snapshot every single order
node server.js 20 1
```

The demo automatically **loops** — when all 500K orders finish replaying, the engine restarts after a 3-second pause.

---

## 🏗️ Architecture Decisions

### 1. Flat Array Price Index (vs `std::map`)

`std::map<price, PriceLevel>` is a red-black tree. Every lookup is O(log n) with guaranteed pointer chasing — each node is a separate heap allocation scattered across memory. The CPU prefetcher cannot predict the access pattern.

**NanoMatch** uses a **direct flat array**: `PriceLevel levels[MAX_PRICE - MIN_PRICE + 1]`. Price lookup is `levels[price - MIN_PRICE]` — a single array index, O(1), spatially contiguous. Cache miss rate drops from ~12% to <1%.

### 2. Memory Pool (vs `new`/`delete`)

`new Order()` calls `malloc`, which may invoke `brk()` or `mmap()` — kernel system calls that introduce microsecond latency spikes in p99. Under load, heap fragmentation worsens over time.

The **memory pool** pre-allocates 1,000,000 `Order` objects in a contiguous flat array at startup. `alloc()` = decrement a stack pointer. `dealloc()` = push index onto free stack. **Zero syscalls** in the hot path.

### 3. Intrusive Linked List (vs `std::deque`)

`std::deque<Order*>` requires two memory indirections (chunk → pointer → Order). NanoMatch embeds `Order* next` directly inside the `Order` struct. Orders from the pool are spatially adjacent, so iterating the list walks memory nearly sequentially.

### 4. Lock-Free SPSC Ring Buffer (vs `std::mutex` queue)

A mutex for trade logging means the matching engine can block waiting for the logger. One slow disk write = the entire engine stalls.

The **SPSC ring buffer** uses only `std::atomic` with acquire/release semantics — CPU memory fences, not kernel calls. `head_` and `tail_` are on separate `alignas(64)` cache lines to prevent **false sharing**.

### 5. Zero-Copy ITCH Binary Parser (vs text CSV)

CSV parsing requires scanning for commas, converting ASCII digits to integers, and copying strings. The ITCH parser uses `mmap` (Linux) / `MapViewOfFile` (Windows) to map the file directly into virtual memory. Byte pointers are cast to packed structs — **zero copies**. Byte-swapping uses hardware intrinsics (`__builtin_bswap` / `_byteswap_`) for single-instruction conversion.

### 6. Lazy Deletion for Cancels

Removing from the middle of a singly-linked list is O(n). Instead, `cancel_order()` sets `o->cancelled = true` in O(1). Cancelled orders are cleaned up when they reach the front during matching. Amortized O(1) per cancel.

---

## 📊 Performance Results

Run `./bench_standalone data/orders_500k.csv` to reproduce these numbers on your machine:

| Metric | Optimized Engine | Naive STL Baseline | Improvement |
|---|---|---|---|
| **Wall Time** | 83.5 ms | 160.7 ms | **~1.9x faster** |
| **Throughput** | 5.99 M orders/sec | 3.11 M orders/sec | **1.9x higher** |
| **p50 Latency** | 140 ns | 245 ns | **1.8x lower** |
| **p99 Latency** | 973 ns | 1,370 ns | **1.4x lower** |
| **Trades** | 363,008 | 363,008 | ✓ match |

> **Note:** These numbers are from a Windows/MSVC environment. On Linux/GCC, the gap widens significantly (~10-14x) because the GNU `std::map` allocator carries heavier overhead compared to MSVC's optimized implementation.

### Key Concepts Demonstrated

| Concept | Where |
|---|---|
| Cache locality (L1/L2/L3) | Memory pool + array price index |
| False sharing prevention | `alignas(64)` on SPSC head/tail |
| Lock-free concurrency | SPSC ring buffer, `memory_order_acquire/release` |
| Memory ordering semantics | `spsc_queue.hpp` comments |
| Custom memory pools | `memory_pool.hpp` |
| Struct packing | `order.hpp` (`static_assert(sizeof(Order) == 48)`) |
| Branch prediction hints | `__builtin_expect` / MSVC `UNLIKELY` macro |
| Zero-copy I/O | `itch_parser.hpp` (mmap) + `csv_parser.hpp` (fread) |
| STL baseline comparison | `order_book_naive.hpp` vs `order_book.hpp` |

---

## ✅ Test Suite — 18/18 Passing ✓

Run with `./run_tests` (or `run_tests.exe` on Windows).

| Test | What It Verifies |
|---|---|
| FullFill | Buy + sell same price/qty → 1 trade, empty book |
| PartialFill_BuyLarger | Buyer has more qty → sell fully matched, buy rests |
| PartialFill_SellLarger | Seller has more qty → buy fully matched, sell rests |
| PriceTimePriority | Two buys at same price → first arrival filled first |
| PricePriority_BestFirst | Two sells at different prices → cheaper filled first |
| MultiLevelSweep | Market buy sweeps 3 ask levels, partial fill at 3rd |
| MarketBuy_FullFill | Market buy vs resting sell |
| MarketOrder_NoLiquidity | Market order with empty book → no crash |
| MarketSell_FullFill | Market sell vs resting buy |
| Cancel_PreventsFill | Cancel before match → sell rests, no trade |
| Cancel_NonExistent | Cancel unknown ID → no crash |
| Cancel_PartialBook | Cancel one of two at same price → correct qty |
| CrossedBook_Resolves | Incoming buy at 101 vs resting sell at 99 → matches at 99 |
| LargeVolume_PoolRecycling | 100k alternating orders → pool recycles, 50k trades |
| OutOfRangePrice_Ignored | Price = 0 → silently dropped |
| SPSC_SingleThread_FIFO | Push 10k trades, drain → strict FIFO order, payload intact, empty after |
| SPSC_FullQueue_Backpressure | Full ring buffer rejects push (no overwrite), frees slot on pop |
| SPSC_ConcurrentProducerConsumer | **500k trades across 2 real threads** → zero loss, FIFO preserved via atomic acquire/release |

---

## 🔬 Linux Profiling (Flame Graphs + Cache Analysis)

Linux `perf` provides hardware-counter access to L1/L2 cache misses and CPU cycle breakdowns. This requires a Linux environment (native or WSL2 with hardware counter passthrough).

### Automated Script

```bash
chmod +x run_perf.sh
./run_perf.sh
```

This will:
1. Build the engine with `-O3`
2. Run `perf stat` to capture cache miss percentages
3. Run `perf record` to generate a CPU flame graph SVG

### Manual Commands

```bash
# Cache miss analysis
perf stat -e cache-misses,cache-references,L1-dcache-load-misses,instructions,cycles \
    ./nanomatch_simple data/orders_500k.csv

# Flame graph generation (requires Brendan Gregg's FlameGraph tools)
perf record -g --call-graph dwarf ./nanomatch_simple data/orders_500k.csv
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

### Expected Metrics

| Counter | Optimized | Naive (estimated) |
|---|---|---|
| Cache miss rate | <1% | ~12% |
| IPC (instructions/cycle) | ~2.5 | ~0.8 |
| L1-dcache-load-misses | <0.5% | ~8-15% |

---

## 🛠️ Troubleshooting

### Build Errors

| Problem | Solution |
|---|---|
| `error: 'uint64_t' was not declared` | Add `-std=c++17` to your compile command |
| `fatal error: intrin.h: No such file` | You're compiling with GCC on Windows — use MSVC or MinGW |
| `undefined reference to pthread_create` | Add `-lpthread` flag (Linux only, for threaded `main.cpp`) |
| CMake version too old | Upgrade to CMake 3.16+ or use direct `g++` compilation |

### Demo Issues

| Problem | Solution |
|---|---|
| `npm install` fails | Make sure Node.js 16+ is installed (`node --version`) |
| `[engine] Failed to spawn` | The demo server can't find `nanomatch_demo` (or `.exe`). Build it first and ensure it's in the project root directory |
| Port 3000 already in use | Kill the process on 3000 or change `PORT` in `demo/server.js` |
| Browser shows "Connecting..." forever | Check that the terminal running `npm start` shows `[engine] Starting...` and isn't printing errors |
| Demo starts but no data appears | Make sure `data/orders_500k.csv` exists. Run `python3 data/gen_orders.py -n 500000 -o data/orders_500k.csv` |

### Data Generation

| Problem | Solution |
|---|---|
| `python3: command not found` | Try `python` instead, or install Python 3 |
| Generated CSV is empty | Check write permissions in the `data/` directory |

---

## 📄 License

This project was built as part of the **FEC (Foundation of Engineering Computing)** course at **IIT Guwahati**, DIY '26.
