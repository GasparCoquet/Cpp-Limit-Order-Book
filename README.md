# High-Performance Limit Order Book (C++)

![C++](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)

A low-latency Limit Order Book (LOB) matching engine simulation implemented in **Modern C++ (C++17)**.
This project focuses on microstructure mechanics, efficient memory management, and algorithmic complexity optimization for HFT scenarios.

## 🚀 Key Features

* **O(1) Order Execution:** Implements constant-time lookups for order cancellation and modification using a dedicated `std::unordered_map` indexing layer.
* **Price-Time Priority:** Standard matching algorithm ensuring fair execution based on price competitiveness and arrival time.
* **Low Latency Architecture:**
    * Uses `std::map` (Red-Black Tree) for ordered price levels to maintain a sorted book.
    * Optimized using `std::list` (doubly linked list) for O(1) insertions/deletions at price levels; O(1) erase via list iterators for cancel/modify.
* **Robust Simulation:** Supports standard order types (Limit, Market, Cancel, Modify).

## 🛠️ Technical Architecture

The engine uses a dual-structure approach to balance **Ordering** (needed for matching) and **Lookup Speed** (needed for management):

1.  **The Book (Bids & Asks):**
    * Stored as `std::map<Price, Level>`.
    * Keeps orders sorted by price automatically.
    * Inside each `Level`, orders are a FIFO queue to respect Time priority.

2.  **The Order Index:**
    * Stored as `std::unordered_map<OrderId, OrderIterator>`.
    * Maps a unique Order ID directly to its location in memory.
    * **Result:** `CancelOrder(id)` is **O(1)** instead of O(N) or O(log N).

## ⚡ Benchmark Results

Latency measured per operation using `std::chrono::high_resolution_clock`, compiled with `-O3` (GCC 13.3, Linux x86-64).

**Machine:** Intel Core Ultra 7 265U (12C/14T), 32 GB DDR5

### Shallow book (100 resting orders)

| Operation | Min (ns) | Mean (ns) | p50 (ns) | p95 (ns) | p99 (ns) | Max (ns) |
|---|---:|---:|---:|---:|---:|---:|
| Add limit (no match) | 63 | 244 | 96 | 152 | 2,092 | 1,187,163 |
| Add limit (immediate match) | 67 | 88 | 76 | 134 | 178 | 8,438 |
| Cancel order | 60 | 95 | 93 | 118 | 155 | 12,676 |
| Modify order | 91 | 127 | 120 | 168 | 197 | 16,714 |
| Market order (5 levels) | 249 | 271 | 257 | 334 | 380 | 26,005 |
| getBestBid + getBestAsk | 30 | 34 | 33 | 39 | 43 | 9,137 |
| getVolumeAtPrice | 30 | 33 | 32 | 34 | 41 | 9,907 |
| Mixed workload | 39 | 147 | 125 | 309 | 430 | 48,098 |

### Deep book (100,000 resting orders)

| Operation | Min (ns) | Mean (ns) | p50 (ns) | p95 (ns) | p99 (ns) | Max (ns) |
|---|---:|---:|---:|---:|---:|---:|
| Add limit (no match) | 63 | 253 | 95 | 157 | 2,061 | 648,075 |
| Cancel order | 60 | 99 | 92 | 115 | 166 | 11,260 |
| Modify order | 104 | 179 | 170 | 226 | 291 | 17,297 |
| Market order (50 levels) | 2,755 | 3,165 | 3,034 | 3,833 | 4,237 | 23,097 |
| getBestBid + getBestAsk | 29 | 33 | 32 | 34 | 38 | 10,139 |
| getVolumeAtPrice | 29 | 32 | 31 | 33 | 33 | 20,980 |
| Mixed workload | 45 | 280 | 217 | 608 | 764 | 14,177 |

> Cancel stays flat at ~92 ns regardless of book depth, confirming O(1) behavior. Best bid/ask queries are ~32 ns. Run `./latency_bench` to reproduce (`cmake --build . --target latency_bench`).

## 📦 Build & Run

### Prerequisites
* C++17 compliant compiler (GCC, Clang, MSVC)
* CMake 3.10+

### Compilation
```bash
mkdir build && cd build
cmake ..
cmake --build .
```