# High-Performance Limit Order Book (C++)

![C++](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

A low-latency Limit Order Book (LOB) matching engine simulation implemented in **Modern C++ (C++17)**.
This project focuses on microstructure mechanics, efficient memory management, and algorithmic complexity optimization for HFT scenarios.

## 🚀 Key Features

* **Fast Order Lookup:** A dedicated `std::unordered_map` indexing layer resolves an `OrderId` to its position in the book in O(1) average time.
* **Price-Time Priority:** Standard matching algorithm ensuring fair execution based on price competitiveness and arrival time.
* **Layered Data Structures:**
    * Uses `std::map` (Red-Black Tree) for ordered price levels to maintain a sorted book; locating a price level is O(log M) in the number of price levels.
    * Within a price level, orders are held in a `std::list` (doubly linked list), so appending an order and erasing one via a cached iterator are both O(1).
* **Robust Simulation:** Supports standard order types (Limit, Market, Cancel, Modify).

## 🛠️ Technical Architecture

The engine uses a dual-structure approach to balance **Ordering** (needed for matching) and **Lookup Speed** (needed for management):

1.  **The Book (Bids & Asks):**
    * Stored as `std::map<Price, Level>`.
    * Keeps orders sorted by price automatically.
    * Inside each `Level`, orders are a FIFO queue to respect Time priority.

2.  **The Order Index:**
    * Stored as `std::unordered_map<OrderId, OrderLocation>`.
    * Maps a unique Order ID directly to its list iterator, price and side.
    * **Result:** `cancelOrder(id)` is **O(log M)** in the number of *price levels* M, and O(1) amortized in the number of *orders* N. The index lookup and the `std::list::erase` are both O(1); the residual O(log M) comes from `removeFromBook` re-locating the price level with `std::map::find`, because `OrderLocation` stores a `Price` rather than a map iterator.

> **Known limitation:** caching the price-level iterator in `OrderLocation` would make cancellation genuinely O(1). This is tracked as a planned change; the README will not claim O(1) until the code delivers it.

## ⚡ Benchmarks

A latency benchmark harness lives in `benchmarks/latency_benchmarks.cpp` and can be built and run locally:

```bash
cmake --build . --target latency_bench
./latency_bench
```

No benchmark figures are published here. The previous numbers were not reproducible on the hardware available to the author, so they have been removed rather than left standing. A measured, reproducible benchmark — with the machine and methodology stated — is being reworked and will be published only once it can be regenerated from this repository.

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

## 📄 License

Released under the MIT License. See [LICENSE](LICENSE).