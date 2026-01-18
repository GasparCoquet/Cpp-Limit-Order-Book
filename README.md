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
    * Optimized using `std::list` (or `std::deque`) for O(1) insertions/deletions at price levels.
    * Designed with cache locality in mind to minimize page faults during high-throughput replay.
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

## 📦 Build & Run

### Prerequisites
* C++17 compliant compiler (GCC, Clang, MSVC)
* CMake 3.10+

### Compilation
```bash
mkdir build && cd build
cmake ..
make