# High-Performance Limit Order Book (C++)

[![CI](https://github.com/GasparCoquet/Cpp-Limit-Order-Book/actions/workflows/ci.yml/badge.svg)](https://github.com/GasparCoquet/Cpp-Limit-Order-Book/actions/workflows/ci.yml)
![C++](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

A low-latency Limit Order Book (LOB) matching engine simulation implemented in **Modern C++ (C++17)**.
This project focuses on microstructure mechanics, efficient memory management, and algorithmic complexity optimization for HFT scenarios.

## Key Features

* **O(1) cancel and amend.** An `unordered_map` index resolves an `OrderId` to a cached `std::list` iterator (the order's node in its price level) *and* a cached `std::map` iterator (the level's node in the price book). Cancelling touches neither search structure: it is an O(1) hash probe, an O(1) `list::erase`, and an amortized-O(1) `map::erase`.
* **Correct price-time priority**, including amend semantics that most toy books get wrong:
  * a size **decrease** is applied in place and **retains** queue position,
  * a size **increase** or a **price change** forfeits priority and re-queues at the back,
  * an amend to zero quantity is a cancel.
* **Duplicate order IDs are rejected**, rather than silently orphaning the existing order.
* **Layered data structures:** `std::map` (red-black tree) for ordered price levels; a `std::list` FIFO queue within each level.
* **Order types:** Limit, Market, Cancel, Modify.

## Technical Architecture

The engine uses a dual-structure approach to balance **ordering** (needed for matching) and **lookup speed** (needed for order management):

1. **The Book (Bids & Asks)**: `std::map<Price, PriceLevel>`, one map per side, each with its own comparator so that `begin()` is always the best price. Within a `PriceLevel`, orders sit in a `std::list` FIFO queue to respect time priority.

2. **The Order Index**: `unordered_map<OrderId, Location>`, where a `Location` caches **both** iterators:

   ```cpp
   template <typename LevelIt>
   struct Location {
       std::list<Order>::iterator orderIt;  // the order's node in the level's FIFO
       LevelIt                    levelIt;  // the level's node in the price map
   };
   ```

   Caching the *level iterator* rather than the level's `Price` is what makes cancellation genuinely O(1): `removeFromBook` never has to re-locate the level with an O(log M) `std::map::find`. This is sound because `std::map` is node-based; its iterators stay valid across inserts and across erasure of *other* nodes, so an iterator captured at insert time is good for the whole life of the order.

   The index is **split by side** (`bidIndex_` / `askIndex_`) because each side's location holds a differently-typed map iterator. A `std::variant` of the two is not an option: a map iterator's type is not parameterised by the map's comparator, so on libstdc++ both sides name the same `_Rb_tree_iterator` and the variant would have duplicate alternatives, and the standard guarantees nothing either way. Splitting the index sidesteps the question and stays type-safe. A lookup by id alone probes both: two O(1) hash probes.

### Complexity

| Operation | Complexity | Notes |
|---|---|---|
| `addOrder` (resting) | O(log M) | `std::map` insert to find/create the price level |
| `cancelOrder` | **O(1)** | hash probe + `list::erase` + amortized `map::erase` |
| `modifyOrder` (size decrease) | **O(1)** | in place; nothing moves |
| `modifyOrder` (reprice / size up) | O(log M) | remove + re-add at the new level |
| `getBestBid` / `getBestAsk` | O(1) | `map::begin()` |
| `getVolumeAtPrice` | O(log M) | `map::find` |
| Matching | O(levels swept) | plus O(1) per order filled |

M = number of distinct price levels; N = number of resting orders. Note that no operation is O(N).

## Benchmarks

Latency figures below are **verbatim CI output** from [`.github/workflows/ci.yml`](.github/workflows/ci.yml); you can reproduce them by re-running the workflow, or locally with `cmake --build build --target latency_bench && ./build/latency_bench`.

**Hardware:** GitHub Actions `ubuntu-latest` runner: AMD EPYC 7763 64-Core Processor, 4 vCPU, g++ 13.3.0, `-O3`.
This is a shared, virtualised CI runner. **Read the shape of each column, not the absolute numbers.** The point of this harness is to make the complexity claims falsifiable, not to advertise nanosecond figures.

Each sample is measured as `now() - now()` around one operation, so it carries one clock read inside it. That floor is measured explicitly and subtracted in the `p50-ovh` column:

```
TIMER OVERHEAD BASELINE (empty loop, two back-to-back steady_clock::now())
  p50 = 30.0 ns   mean = 29.2 ns   min = 20.0 ns
```

At a ~30 ns floor with ~10 ns of quantisation, several of these operations are at or below what this harness can resolve. Rows that land under the floor are reported as `<floor`, not as a number.

### Cancel is O(1): the actual experiment

Sweep the price-level count **M** over three orders of magnitude with the order count N held constant. O(1) in M predicts a flat column; a hidden O(log M) `map::find` would climb.

```
Cancel                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
cancel M=10                    50000         51           21         61         71         55
cancel M=100                   50000         51           21         61         71         56
cancel M=1000                  50000         60           30         61         90         57
cancel M=10000                 50000         60           30         61         90         57
```

Now sweep the order count **N** with M fixed:

```
Cancel                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
cancel N=10000                 10000         60           30         61         80         56
cancel N=50000                 50000         60           30         61         80         56
cancel N=200000               200000         60           30         61         71         56
```

Flat in both, within the harness's resolution.

### The control: an operation that is *not* O(1)

`getVolumeAtPrice` is a `std::map::find` and is therefore genuinely O(log M). Its column is *expected* to climb, and it does. This is the control that shows the flat cancel column above is a real measurement and not an artifact of the setup:

```
getVolumeAtPrice                 Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
volume M=10                    50000         40           10         41         41         39
volume M=100                   50000         40           10         41         41         40
volume M=1000                  50000         40           10         60         71         45
volume M=10000                 50000         70           40        131        211         78
```

### Amend: the two paths

```
Modify                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
modify in-place (shrink)       50000         30       <floor         40         40         31
modify requeue (reprice)       50000         80           50        100        120         86
```

An in-place shrink moves nothing and lands below the timer floor. A reprice is a remove + re-add. The gap between these two rows is the cost the engine used to pay on *every* amend, along with the queue position it silently threw away.

### Other operations

```
Operation                        Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
getBestBid+getBestAsk          50000         41           11         51         51         45
market order (5 levels)         5000        591          561        622        662        503
market order (50 levels)        2000       3657         3627       6223       6794       4542
mixed 50/30/20 (10k book)      50000        160          130        371        531        193
```

## Tests

19 tests, dependency-free, run in CI in **both Debug and Release**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/verify        # nonzero exit on any failure
```

Assertions use a `CHECK` macro rather than `assert()`. This matters: `CMAKE_BUILD_TYPE=Release` defines `NDEBUG`, which compiles `assert()` to a no-op, so a suite built on bare `assert()` verifies **nothing** in the configuration it actually ships, while still printing a pass. `CHECK` is an ordinary runtime branch that cannot be compiled away, and the runner aborts if zero checks ever execute.

## Build & Run

### Prerequisites
* C++17 compliant compiler (GCC, Clang, MSVC)
* CMake 3.10+

### Compilation
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/orderbook       # demo
./build/verify          # test suite
./build/latency_bench   # benchmarks
```

## License

Released under the MIT License. See [LICENSE](LICENSE).
