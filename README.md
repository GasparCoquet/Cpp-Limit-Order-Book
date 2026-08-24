# High-Performance Limit Order Book (C++)

[![CI](https://github.com/GasparCoquet/Cpp-Limit-Order-Book/actions/workflows/ci.yml/badge.svg)](https://github.com/GasparCoquet/Cpp-Limit-Order-Book/actions/workflows/ci.yml)
![C++](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

A low-latency Limit Order Book (LOB) matching engine simulation implemented in **Modern C++ (C++17)**.
This project focuses on microstructure mechanics, efficient memory management, and algorithmic complexity optimization for HFT scenarios.

## Key Features

* **O(1) cancel and amend.** An `unordered_map` index resolves an `OrderId` to a cached `std::list` iterator (the order's node in its price level) *and* a cached `std::map` iterator (the level's node in the price book). Cancelling touches neither search structure. It is an O(1) hash probe, an O(1) `list::erase`, and an amortized-O(1) `map::erase`.
* **Correct price-time priority**, including amend semantics that most toy books get wrong:
  * a size **decrease** is applied in place and **retains** queue position,
  * a size **increase** or a **price change** forfeits priority and re-queues at the back,
  * an amend to zero quantity is a cancel.
* **Duplicate order IDs are rejected**, rather than silently orphaning the existing order.
* **Layered data structures.** `std::map` (red-black tree) for ordered price levels, with a `std::list` FIFO queue within each level.
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

   Caching the *level iterator* rather than the level's `Price` is what makes cancellation genuinely O(1). `removeFromBook` never has to re-locate the level with an O(log M) `std::map::find`. This is sound because `std::map` is node-based, and its iterators stay valid across inserts and across erasure of *other* nodes, so an iterator captured at insert time is good for the whole life of the order.

   The index is **split by side** (`bidIndex_` / `askIndex_`) because each side's location holds a differently-typed map iterator. A `std::variant` of the two is not an option, because a map iterator's type is not parameterised by the map's comparator, so on libstdc++ both sides name the same `_Rb_tree_iterator` and the variant would have duplicate alternatives, and the standard guarantees nothing either way. Splitting the index sidesteps the question and stays type-safe. A lookup by id alone probes both, at a cost of two O(1) hash probes.

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

M = number of distinct price levels, N = number of resting orders. No *book-keeping* operation is O(N). Add, cancel, amend and best-quote lookup are all independent of how many orders rest in the book. Matching is the exception, and unavoidably so, because a marketable order that consumes K resting orders costs O(K), and K is bounded only by N. The two `market order` rows in the benchmarks below measure exactly that.

## Benchmarks

Every figure below is copied from one CI run, [job 97199996989](https://github.com/GasparCoquet/Cpp-Limit-Order-Book/actions/runs/32641797238), the benchmark leg of the workflow on the current HEAD. Re-run the workflow and you will get the same shapes on different absolute numbers, because GitHub does not guarantee which host you land on. That run drew an **AMD EPYC 9V45 96-Core Processor**, an earlier one on the same code drew an EPYC 7763 and was roughly 2x slower. Locally, run `cmake --build build --target latency_bench && ./build/latency_bench`.

**Read the shape of each column, not the absolute numbers.** This harness exists to make the complexity claims falsifiable on a shared virtualised runner, not to advertise nanosecond figures.

Each sample is measured as `now() - now()` around one operation, so it carries one clock read inside it. That floor is measured explicitly and subtracted in the `p50-ovh` column:

```
TIMER OVERHEAD BASELINE (empty loop, two back-to-back steady_clock::now())
  p50 = 20.0 ns   mean = 21.4 ns   min = 20.0 ns
```

At a ~20 ns floor with ~10 ns of quantisation, several of these operations are at or below what this harness can resolve. Rows that land under the floor are reported as `<floor`, not as a number, and any difference of one quantum is noise.

### The actual O(1) cancel experiment

Sweep the price-level count **M** over three orders of magnitude with the order count N held constant. O(1) in M predicts a flat column, while a hidden O(log M) `map::find` would climb.

```
Cancel                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
cancel M=10                    50000         30           10         40         41         33
cancel M=100                   50000         30           10         40         41         33
cancel M=1000                  50000         30           10         40         41         33
cancel M=10000                 50000         30           10         40         50         34
```

Now sweep the order count **N** with M fixed:

```
Cancel                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
cancel N=10000                 10000         30           10         40         50         33
cancel N=50000                 50000         30           10         40         50         34
cancel N=200000               200000         30           10         40         50         33
```

Flat in both, across a 20x sweep in M and N.

### The control, an operation that is *not* O(1)

A flat column proves nothing on its own; it could just mean the harness cannot see anything. So the same binary measures an operation that is *known* to be O(log M) and must climb. `addOrder` inserts into the `std::map` of price levels:

```
Add limit (no match)             Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
add M=10                       50000         40           20         51         70         49
add M=100                      50000         50           30         60         71         50
add M=1000                     50000         60           40         80         90         65
add M=10000                    50000         80           60        110        141         83
```

That is the ramp the cancel column would show if the O(1) claim were false. Same harness, same run, same order of magnitude in M.

`getVolumeAtPrice` is also a `map::find` and is measured too, but it is a weaker control: at this clock resolution three of its four points are identical and only the last one separates:

```
getVolumeAtPrice                 Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
volume M=10                    50000         30           10         30         31         27
volume M=100                   50000         30           10         40         40         29
volume M=1000                  50000         30           10         50         70         36
volume M=10000                 50000         50           30         90        121         54
```

Read the `add` table as the control and this one as corroboration, not the other way round.

### The two amend paths

```
Modify                           Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
modify in-place (shrink)       50000         20       <floor         30         31         23
modify requeue (reprice)       50000         50           30         60         70         52
```

An in-place shrink moves nothing and lands below the timer floor. A reprice is a remove + re-add. The gap between these two rows is the cost the engine used to pay on *every* amend, along with the queue position it silently threw away.

### Other operations

```
Operation                        Ops    p50(ns)      p50-ovh    p95(ns)    p99(ns)   mean(ns)
---------------------------------------------------------------------------------------------
getBestBid+getBestAsk          50000         30           10         30         31         29
market order (5 levels)         5000        160          140        161        310        162
market order (50 levels)        2000       2013         1993       2103       2214       2031
mixed 50/30/20 (10k book)      50000         90           70        190        270        103
```

The two market-order rows are the linear-in-consumption cost made visible. 10x the levels swept costs about 13x the time. Nothing here is O(1) in what a marketable order actually eats, and the complexity table above says so.

## Tests

19 tests, dependency-free, run in CI in **both Debug and Release**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/verify        # nonzero exit on any failure
```

Assertions use a `CHECK` macro rather than `assert()`. This matters because `CMAKE_BUILD_TYPE=Release` defines `NDEBUG`, which compiles `assert()` to a no-op, so a suite built on bare `assert()` verifies **nothing** in the configuration it actually ships, while still printing a pass. `CHECK` is an ordinary runtime branch that cannot be compiled away, and the runner aborts if zero checks ever execute.

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
