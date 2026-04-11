#include "OrderBook.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

using namespace LOB;
using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Statistics helper
// ---------------------------------------------------------------------------
struct LatencyStats {
    double min_ns;
    double max_ns;
    double mean_ns;
    double median_ns;
    double p50_ns;
    double p95_ns;
    double p99_ns;
    size_t count;
};

static LatencyStats computeStats(std::vector<double>& samples) {
    LatencyStats s{};
    s.count = samples.size();
    if (samples.empty()) return s;

    std::sort(samples.begin(), samples.end());

    s.min_ns  = samples.front();
    s.max_ns  = samples.back();
    s.mean_ns = std::accumulate(samples.begin(), samples.end(), 0.0) / s.count;

    auto percentile = [&](double p) -> double {
        double idx = p / 100.0 * (s.count - 1);
        size_t lo  = static_cast<size_t>(idx);
        size_t hi  = lo + 1;
        if (hi >= s.count) return samples.back();
        double frac = idx - lo;
        return samples[lo] * (1.0 - frac) + samples[hi] * frac;
    };

    s.median_ns = percentile(50);
    s.p50_ns    = s.median_ns;
    s.p95_ns    = percentile(95);
    s.p99_ns    = percentile(99);
    return s;
}

// ---------------------------------------------------------------------------
// Pretty-print helpers
// ---------------------------------------------------------------------------
static void printHeader() {
    std::cout << std::left
              << std::setw(40) << "Benchmark"
              << std::right
              << std::setw(10) << "Ops"
              << std::setw(12) << "Min(ns)"
              << std::setw(12) << "Mean(ns)"
              << std::setw(12) << "p50(ns)"
              << std::setw(12) << "p95(ns)"
              << std::setw(12) << "p99(ns)"
              << std::setw(12) << "Max(ns)"
              << "\n"
              << std::string(122, '-') << "\n";
}

static void printRow(const std::string& name, const LatencyStats& s) {
    std::cout << std::left
              << std::setw(40) << name
              << std::right << std::fixed << std::setprecision(0)
              << std::setw(10) << s.count
              << std::setw(12) << s.min_ns
              << std::setw(12) << s.mean_ns
              << std::setw(12) << s.p50_ns
              << std::setw(12) << s.p95_ns
              << std::setw(12) << s.p99_ns
              << std::setw(12) << s.max_ns
              << "\n";
}

// ---------------------------------------------------------------------------
// Benchmark: Add limit order (no match, resting)
// ---------------------------------------------------------------------------
static LatencyStats benchAddLimitNoMatch(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    OrderBook book;
    // Pre-populate the book with non-overlapping orders
    for (size_t i = 0; i < bookDepth; ++i) {
        book.addOrder(Order(i + 1, Side::BUY, OrderType::LIMIT,
                            5000 - static_cast<Price>(i % 500), 100, 0));
    }

    // Benchmark: add sell orders at prices that won't match any bids
    for (size_t i = 0; i < iterations; ++i) {
        OrderId id = bookDepth + i + 1;
        Price price = 6000 + static_cast<Price>(i % 500);
        Order order(id, Side::SELL, OrderType::LIMIT, price, 100, 0);

        auto t0 = Clock::now();
        book.addOrder(order);
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: Add limit order (immediate full match)
// ---------------------------------------------------------------------------
static LatencyStats benchAddLimitWithMatch(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        OrderBook book;
        // Pre-populate with sell orders
        for (size_t j = 0; j < bookDepth; ++j) {
            book.addOrder(Order(j + 1, Side::SELL, OrderType::LIMIT,
                                10000 + static_cast<Price>(j), 100, 0));
        }

        // Timed: add a buy order that crosses the spread and matches
        Order aggressor(bookDepth + 1, Side::BUY, OrderType::LIMIT, 10000, 100, 0);

        auto t0 = Clock::now();
        book.addOrder(aggressor);
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: Cancel order
// ---------------------------------------------------------------------------
static LatencyStats benchCancelOrder(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    // Build a fresh book and cancel from the middle
    OrderBook book;
    size_t totalOrders = bookDepth + iterations;
    for (size_t i = 0; i < totalOrders; ++i) {
        book.addOrder(Order(i + 1, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(i % 500), 100, 0));
    }

    // Cancel the last `iterations` orders (they exist in the book)
    for (size_t i = 0; i < iterations; ++i) {
        OrderId target = bookDepth + i + 1;

        auto t0 = Clock::now();
        book.cancelOrder(target);
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: Modify order (price + quantity change)
// ---------------------------------------------------------------------------
static LatencyStats benchModifyOrder(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    OrderBook book;
    for (size_t i = 0; i < bookDepth; ++i) {
        book.addOrder(Order(i + 1, Side::BUY, OrderType::LIMIT,
                            5000 - static_cast<Price>(i % 500), 100, 0));
    }

    // Modify orders that exist in the book – cycle through valid IDs
    for (size_t i = 0; i < iterations; ++i) {
        OrderId target = (i % bookDepth) + 1;
        Price newPrice = 4000 - static_cast<Price>(i % 300);

        auto t0 = Clock::now();
        book.modifyOrder(target, newPrice, 150);
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: Market order (sweeps multiple levels)
// ---------------------------------------------------------------------------
static LatencyStats benchMarketOrder(size_t levelsToSweep, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        OrderBook book;
        // Set up `levelsToSweep` price levels, 100 qty each
        for (size_t j = 0; j < levelsToSweep; ++j) {
            book.addOrder(Order(j + 1, Side::SELL, OrderType::LIMIT,
                                10000 + static_cast<Price>(j), 100, 0));
        }

        // Market buy that sweeps all levels
        Quantity totalQty = static_cast<Quantity>(levelsToSweep) * 100;
        Order mkt(levelsToSweep + 1, Side::BUY, OrderType::MARKET, 0, totalQty, 0);

        auto t0 = Clock::now();
        book.addOrder(mkt);
        auto t1 = Clock::now();

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: getBestBid / getBestAsk queries
// ---------------------------------------------------------------------------
static LatencyStats benchGetBestBidAsk(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    OrderBook book;
    for (size_t i = 0; i < bookDepth; ++i) {
        book.addOrder(Order(i + 1, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(i % 500), 100, 0));
        book.addOrder(Order(bookDepth + i + 1, Side::SELL, OrderType::LIMIT,
                            10500 + static_cast<Price>(i % 500), 100, 0));
    }

    volatile Price sink = 0; // prevent dead-code elimination
    for (size_t i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        auto bid = book.getBestBid();
        auto ask = book.getBestAsk();
        auto t1 = Clock::now();

        if (bid) sink = *bid;
        if (ask) sink = *ask;
        (void)sink;

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: getVolumeAtPrice query
// ---------------------------------------------------------------------------
static LatencyStats benchGetVolumeAtPrice(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    OrderBook book;
    Price targetPrice = 10000;
    // All orders at a single price level
    for (size_t i = 0; i < bookDepth; ++i) {
        book.addOrder(Order(i + 1, Side::BUY, OrderType::LIMIT,
                            targetPrice, 100, 0));
    }

    volatile Quantity sink = 0;
    for (size_t i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        auto vol = book.getVolumeAtPrice(Side::BUY, targetPrice);
        auto t1 = Clock::now();

        if (vol) sink = *vol;
        (void)sink;

        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Benchmark: Mixed realistic workload
// ---------------------------------------------------------------------------
static LatencyStats benchMixedWorkload(size_t bookDepth, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    std::mt19937 rng(42); // deterministic seed
    std::uniform_int_distribution<int> actionDist(0, 9);
    std::uniform_int_distribution<Price> priceDist(9000, 11000);
    std::uniform_int_distribution<Quantity> qtyDist(10, 500);

    OrderBook book;
    OrderId nextId = 1;
    std::vector<OrderId> activeIds;
    activeIds.reserve(bookDepth + iterations);

    // Pre-populate
    for (size_t i = 0; i < bookDepth; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Price price = (side == Side::BUY) ? priceDist(rng) - 500 : priceDist(rng) + 500;
        book.addOrder(Order(nextId, side, OrderType::LIMIT, price, qtyDist(rng), 0));
        activeIds.push_back(nextId);
        ++nextId;
    }

    for (size_t i = 0; i < iterations; ++i) {
        int action = actionDist(rng);

        auto t0 = Clock::now();

        if (action < 5) {
            // 50% - add a new limit order
            Side side = (action < 3) ? Side::BUY : Side::SELL;
            Price price = priceDist(rng);
            book.addOrder(Order(nextId, side, OrderType::LIMIT, price, qtyDist(rng), 0));
            activeIds.push_back(nextId);
            ++nextId;
        } else if (action < 8 && !activeIds.empty()) {
            // 30% - cancel an order
            std::uniform_int_distribution<size_t> idxDist(0, activeIds.size() - 1);
            size_t idx = idxDist(rng);
            book.cancelOrder(activeIds[idx]);
            // swap-erase
            activeIds[idx] = activeIds.back();
            activeIds.pop_back();
        } else if (!activeIds.empty()) {
            // 20% - modify an order
            std::uniform_int_distribution<size_t> idxDist(0, activeIds.size() - 1);
            size_t idx = idxDist(rng);
            book.modifyOrder(activeIds[idx], priceDist(rng), qtyDist(rng));
        }

        auto t1 = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n"
              << "================================================================"
                 "============================================================\n"
              << "                     LIMIT ORDER BOOK  -  LATENCY BENCHMARKS\n"
              << "================================================================"
                 "============================================================\n\n";

    // ------- Shallow book (100 orders) -------
    std::cout << ">> Book depth: 100 resting orders\n\n";
    printHeader();
    printRow("Add limit (no match)",        benchAddLimitNoMatch(100, 50000));
    printRow("Add limit (immediate match)", benchAddLimitWithMatch(100, 10000));
    printRow("Cancel order",                benchCancelOrder(100, 50000));
    printRow("Modify order",                benchModifyOrder(100, 50000));
    printRow("Market order (sweep 5 lvls)", benchMarketOrder(5, 10000));
    printRow("getBestBid + getBestAsk",     benchGetBestBidAsk(100, 100000));
    printRow("getVolumeAtPrice",            benchGetVolumeAtPrice(100, 100000));
    printRow("Mixed workload",              benchMixedWorkload(100, 50000));
    std::cout << "\n";

    // ------- Medium book (10,000 orders) -------
    std::cout << ">> Book depth: 10,000 resting orders\n\n";
    printHeader();
    printRow("Add limit (no match)",        benchAddLimitNoMatch(10000, 50000));
    printRow("Add limit (immediate match)", benchAddLimitWithMatch(10000, 5000));
    printRow("Cancel order",                benchCancelOrder(10000, 50000));
    printRow("Modify order",                benchModifyOrder(10000, 50000));
    printRow("Market order (sweep 10 lvls)",benchMarketOrder(10, 5000));
    printRow("getBestBid + getBestAsk",     benchGetBestBidAsk(10000, 100000));
    printRow("getVolumeAtPrice",            benchGetVolumeAtPrice(10000, 100000));
    printRow("Mixed workload",              benchMixedWorkload(10000, 50000));
    std::cout << "\n";

    // ------- Deep book (100,000 orders) -------
    std::cout << ">> Book depth: 100,000 resting orders\n\n";
    printHeader();
    printRow("Add limit (no match)",        benchAddLimitNoMatch(100000, 50000));
    printRow("Add limit (immediate match)", benchAddLimitWithMatch(100, 5000));
    printRow("Cancel order",                benchCancelOrder(100000, 50000));
    printRow("Modify order",                benchModifyOrder(100000, 50000));
    printRow("Market order (sweep 50 lvls)",benchMarketOrder(50, 5000));
    printRow("getBestBid + getBestAsk",     benchGetBestBidAsk(100000, 100000));
    printRow("getVolumeAtPrice",            benchGetVolumeAtPrice(100000, 100000));
    printRow("Mixed workload",              benchMixedWorkload(100000, 50000));

    std::cout << "\n"
              << "================================================================"
                 "============================================================\n"
              << "  All latencies in nanoseconds. Compiled with -O3.\n"
              << "================================================================"
                 "============================================================\n\n";

    return 0;
}
