// Latency benchmarks for the limit order book.
//
// WHAT THIS HARNESS IS FOR
// ------------------------
// The point is to make the complexity claims FALSIFIABLE, not to advertise pretty
// nanosecond figures. Absolute latencies measured on a shared CI runner are noisy
// and machine-specific; what is meaningful is how a cost SCALES as the parameter it
// supposedly does not depend on is swept.
//
// Concretely:
//   - cancelOrder is claimed to be O(1) in the price-level count M. So we sweep
//     M = 10 / 100 / 1000 / 10000 with the order count N held constant. If the claim
//     holds, the latency column is flat. If it secretly did an O(log M) map::find,
//     the column would climb.
//   - getVolumeAtPrice is a std::map::find, i.e. genuinely O(log M). We sweep M and
//     expect it to CLIMB. Reporting an operation as fast when it is actually
//     logarithmic would be the same lie in the other direction.
//
// WHAT THE PREVIOUS VERSION OF THIS FILE GOT WRONG
// ------------------------------------------------
//   - benchCancelOrder priced every order as 10000 - (i % 500), which pins the book
//     at exactly 500 price levels no matter how many orders are inserted. The
//     "shallow" (100-order) and "deep" (100k-order) runs therefore had the SAME M,
//     so cancel cost could not vary between them. The flatness that supposedly
//     "confirmed O(1)" was an artifact of the setup, not a measurement.
//   - benchGetVolumeAtPrice put every order at ONE price, so the map held a single
//     element. It timed a 1-element map lookup and reported it as a 100k-order
//     result.
//   - There was no timer-overhead baseline. std::chrono::now() costs tens of ns, so
//     the reported sub-100ns figures were sitting at the timer floor and were mostly
//     measuring the clock, not the book.
//   - There was no warmup, so the first samples paid for cold caches and allocator
//     growth.
//
// All four are fixed below.

#include "OrderBook.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace LOB;

// steady_clock, not high_resolution_clock: on libstdc++ high_resolution_clock is an
// alias for system_clock, which is NOT monotonic and can jump under NTP.
using Clock = std::chrono::steady_clock;

namespace {

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
struct LatencyStats {
    double min_ns = 0;
    double mean_ns = 0;
    double p50_ns = 0;
    double p95_ns = 0;
    double p99_ns = 0;
    double max_ns = 0;
    size_t count = 0;
};

LatencyStats computeStats(std::vector<double>& samples) {
    LatencyStats s{};
    s.count = samples.size();
    if (samples.empty()) return s;

    std::sort(samples.begin(), samples.end());

    s.min_ns = samples.front();
    s.max_ns = samples.back();
    s.mean_ns = std::accumulate(samples.begin(), samples.end(), 0.0) /
                static_cast<double>(s.count);

    auto percentile = [&](double p) -> double {
        double idx = p / 100.0 * static_cast<double>(s.count - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = lo + 1;
        if (hi >= s.count) return samples.back();
        double frac = idx - static_cast<double>(lo);
        return samples[lo] * (1.0 - frac) + samples[hi] * frac;
    };

    s.p50_ns = percentile(50);
    s.p95_ns = percentile(95);
    s.p99_ns = percentile(99);
    return s;
}

// ---------------------------------------------------------------------------
// Timer overhead baseline
// ---------------------------------------------------------------------------
// Every timed sample below is measured as (now() - now()) around one operation, so
// each sample carries roughly one now() call's latency inside it. This measures that
// floor with an empty loop, so it can be subtracted and reported explicitly rather
// than being silently folded into the results.
double g_overheadP50 = 0.0;

LatencyStats measureTimerOverhead(size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < 10000; ++i) {  // warmup
        auto a = Clock::now();
        auto b = Clock::now();
        (void)a;
        (void)b;
    }

    for (size_t i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        auto t1 = Clock::now();  // nothing between the two reads
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------
void printHeader(const std::string& firstCol) {
    std::cout << std::left << std::setw(26) << firstCol << std::right
              << std::setw(10) << "Ops"
              << std::setw(11) << "p50(ns)"
              << std::setw(13) << "p50-ovh"
              << std::setw(11) << "p95(ns)"
              << std::setw(11) << "p99(ns)"
              << std::setw(11) << "mean(ns)"
              << "\n"
              << std::string(93, '-') << "\n";
}

// p50 with the timer floor subtracted. If the operation is at or under the floor we
// say so instead of printing a number the harness cannot actually resolve.
std::string netOfOverhead(double p50) {
    double v = p50 - g_overheadP50;
    if (v <= 0.0) return "<floor";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

void printRow(const std::string& label, const LatencyStats& s) {
    std::cout << std::left << std::setw(26) << label << std::right << std::fixed
              << std::setprecision(0)
              << std::setw(10) << s.count
              << std::setw(11) << s.p50_ns
              << std::setw(13) << netOfOverhead(s.p50_ns)
              << std::setw(11) << s.p95_ns
              << std::setw(11) << s.p99_ns
              << std::setw(11) << s.mean_ns
              << "\n";
}

// ---------------------------------------------------------------------------
// Cancel: sweep the price-level count M, hold the order count N constant.
// ---------------------------------------------------------------------------
// This is the experiment the O(1) claim actually rests on. `numLevels` is a real,
// controlled variable here -- the old harness could not vary it at all.
LatencyStats benchCancelVsLevels(size_t numLevels, size_t iterations) {
    OrderBook book;
    OrderId nextId = 1;

    // Ballast: one resting order per level that is never cancelled. This pins the
    // book at exactly `numLevels` levels for the whole timed run, so no timed cancel
    // ever empties a level. M stays fixed by construction instead of drifting as the
    // measurement proceeds.
    for (size_t m = 0; m < numLevels; ++m) {
        book.addOrder(Order(nextId++, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(m), 100, 0));
    }

    // The orders we will actually cancel, spread round-robin over the M levels.
    std::vector<OrderId> victims;
    victims.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        OrderId id = nextId++;
        book.addOrder(Order(id, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(i % numLevels), 100, 0));
        victims.push_back(id);
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        book.cancelOrder(victims[i]);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Cancel: sweep the order count N, hold the price-level count M constant.
// ---------------------------------------------------------------------------
// The other half of the O(1) claim: cancel must not care how many orders are resting.
LatencyStats benchCancelVsOrders(size_t numOrders, size_t numLevels) {
    OrderBook book;
    OrderId nextId = 1;

    for (size_t m = 0; m < numLevels; ++m) {
        book.addOrder(Order(nextId++, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(m), 100, 0));
    }

    std::vector<OrderId> victims;
    victims.reserve(numOrders);
    for (size_t i = 0; i < numOrders; ++i) {
        OrderId id = nextId++;
        book.addOrder(Order(id, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(i % numLevels), 100, 0));
        victims.push_back(id);
    }

    std::vector<double> samples;
    samples.reserve(numOrders);
    for (size_t i = 0; i < numOrders; ++i) {
        auto t0 = Clock::now();
        book.cancelOrder(victims[i]);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// getVolumeAtPrice: sweep M. This is a std::map::find, so it is O(log M) and the
// column SHOULD climb. Queries cycle through all M prices rather than hammering one
// hot node, which is what the old harness accidentally did.
// ---------------------------------------------------------------------------
LatencyStats benchVolumeVsLevels(size_t numLevels, size_t iterations) {
    OrderBook book;
    OrderId nextId = 1;
    const size_t ordersPerLevel = 10;

    std::vector<Price> prices;
    prices.reserve(numLevels);
    for (size_t m = 0; m < numLevels; ++m) {
        Price p = 10000 - static_cast<Price>(m);
        prices.push_back(p);
        for (size_t k = 0; k < ordersPerLevel; ++k) {
            book.addOrder(Order(nextId++, Side::BUY, OrderType::LIMIT, p, 100, 0));
        }
    }

    volatile Quantity sink = 0;
    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        Price p = prices[i % numLevels];
        auto t0 = Clock::now();
        auto vol = book.getVolumeAtPrice(Side::BUY, p);
        auto t1 = Clock::now();
        if (vol) sink = *vol;
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    (void)sink;
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Add a resting limit order (no match), sweeping M.
// ---------------------------------------------------------------------------
LatencyStats benchAddLimitVsLevels(size_t numLevels, size_t iterations) {
    OrderBook book;
    OrderId nextId = 1;

    // Bids only, and the incoming orders are also bids, so nothing ever crosses:
    // this times the pure "rest in the book" path with no matching mixed in.
    for (size_t m = 0; m < numLevels; ++m) {
        book.addOrder(Order(nextId++, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(m), 100, 0));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        Price p = 10000 - static_cast<Price>(i % numLevels);
        Order o(nextId++, Side::BUY, OrderType::LIMIT, p, 100, 0);

        auto t0 = Clock::now();
        book.addOrder(o);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Modify, IN-PLACE path: same price, strictly decreasing size. Keeps queue priority,
// touches no map node, moves no list node.
// ---------------------------------------------------------------------------
LatencyStats benchModifyInPlace(size_t numLevels, size_t iterations) {
    OrderBook book;
    const size_t numOrders = 1000;
    const Quantity startQty = 1000000;

    std::vector<Price> priceOf(numOrders);
    for (size_t i = 0; i < numOrders; ++i) {
        Price p = 10000 - static_cast<Price>(i % numLevels);
        priceOf[i] = p;
        book.addOrder(Order(static_cast<OrderId>(i + 1), Side::BUY, OrderType::LIMIT,
                            p, startQty, 0));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        size_t idx = i % numOrders;
        // Strictly decreasing on each pass over the same order, so every call really
        // does take the in-place shrink path.
        Quantity newQty = startQty - static_cast<Quantity>(i / numOrders) - 1;

        auto t0 = Clock::now();
        book.modifyOrder(static_cast<OrderId>(idx + 1), priceOf[idx], newQty);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Modify, REQUEUE path: price change. Forfeits priority: remove + re-add at the back.
// Strictly more work than the in-place path, and the gap between the two rows is the
// cost the old implementation was silently charging on EVERY amend.
// ---------------------------------------------------------------------------
LatencyStats benchModifyRequeue(size_t numLevels, size_t iterations) {
    OrderBook book;
    const size_t numOrders = 1000;
    const Price maxPrice = 10000;
    const Price minPrice = 10000 - static_cast<Price>(numLevels) + 1;

    std::vector<Price> curPrice(numOrders);
    for (size_t i = 0; i < numOrders; ++i) {
        Price p = maxPrice - static_cast<Price>(i % numLevels);
        curPrice[i] = p;
        book.addOrder(Order(static_cast<OrderId>(i + 1), Side::BUY, OrderType::LIMIT,
                            p, 100, 0));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        size_t idx = i % numOrders;
        // Walk the order down one tick, wrapping inside the band. Always a genuine
        // price CHANGE, so this always takes the requeue path.
        Price next = curPrice[idx] - 1;
        if (next < minPrice) next = maxPrice;

        auto t0 = Clock::now();
        book.modifyOrder(static_cast<OrderId>(idx + 1), next, 100);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());

        curPrice[idx] = next;
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// getBestBid + getBestAsk: map::begin() on both books, O(1).
// ---------------------------------------------------------------------------
LatencyStats benchGetBestBidAsk(size_t numLevels, size_t iterations) {
    OrderBook book;
    OrderId nextId = 1;
    for (size_t m = 0; m < numLevels; ++m) {
        book.addOrder(Order(nextId++, Side::BUY, OrderType::LIMIT,
                            10000 - static_cast<Price>(m), 100, 0));
        book.addOrder(Order(nextId++, Side::SELL, OrderType::LIMIT,
                            10500 + static_cast<Price>(m), 100, 0));
    }

    volatile Price sink = 0;
    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        auto t0 = Clock::now();
        auto bid = book.getBestBid();
        auto ask = book.getBestAsk();
        auto t1 = Clock::now();
        if (bid) sink = *bid;
        if (ask) sink = *ask;
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    (void)sink;
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Market order sweeping a given number of levels. Cost is O(levels swept), by design.
// ---------------------------------------------------------------------------
LatencyStats benchMarketOrder(size_t levelsToSweep, size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        OrderBook book;
        for (size_t j = 0; j < levelsToSweep; ++j) {
            book.addOrder(Order(static_cast<OrderId>(j + 1), Side::SELL,
                                OrderType::LIMIT, 10000 + static_cast<Price>(j), 100, 0));
        }

        Quantity totalQty = static_cast<Quantity>(levelsToSweep) * 100;
        Order mkt(static_cast<OrderId>(levelsToSweep + 1), Side::BUY,
                  OrderType::MARKET, 0, totalQty, 0);

        auto t0 = Clock::now();
        book.addOrder(mkt);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

// ---------------------------------------------------------------------------
// Mixed workload: 50% add, 30% cancel, 20% modify, deterministic seed.
// ---------------------------------------------------------------------------
LatencyStats benchMixedWorkload(size_t bookDepth, size_t iterations) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> actionDist(0, 9);
    std::uniform_int_distribution<Price> priceDist(9000, 11000);
    std::uniform_int_distribution<Quantity> qtyDist(10, 500);

    OrderBook book;
    OrderId nextId = 1;
    std::vector<OrderId> activeIds;
    activeIds.reserve(bookDepth + iterations);

    for (size_t i = 0; i < bookDepth; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Price price = (side == Side::BUY) ? priceDist(rng) - 500 : priceDist(rng) + 500;
        book.addOrder(Order(nextId, side, OrderType::LIMIT, price, qtyDist(rng), 0));
        activeIds.push_back(nextId);
        ++nextId;
    }

    std::vector<double> samples;
    samples.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        int action = actionDist(rng);

        auto t0 = Clock::now();
        if (action < 5) {
            Side side = (action < 3) ? Side::BUY : Side::SELL;
            book.addOrder(Order(nextId, side, OrderType::LIMIT, priceDist(rng),
                                qtyDist(rng), 0));
            activeIds.push_back(nextId);
            ++nextId;
        } else if (action < 8 && !activeIds.empty()) {
            std::uniform_int_distribution<size_t> idxDist(0, activeIds.size() - 1);
            size_t idx = idxDist(rng);
            book.cancelOrder(activeIds[idx]);
            activeIds[idx] = activeIds.back();
            activeIds.pop_back();
        } else if (!activeIds.empty()) {
            std::uniform_int_distribution<size_t> idxDist(0, activeIds.size() - 1);
            size_t idx = idxDist(rng);
            book.modifyOrder(activeIds[idx], priceDist(rng), qtyDist(rng));
        }
        auto t1 = Clock::now();

        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return computeStats(samples);
}

std::string levelLabel(const char* prefix, size_t m) {
    return std::string(prefix) + " M=" + std::to_string(m);
}

}  // namespace

// ---------------------------------------------------------------------------
int main() {
    const size_t kIters = 50000;
    const size_t kWarmup = 5000;
    const std::vector<size_t> kLevels = {10, 100, 1000, 10000};

    std::cout << "\n============================================================"
                 "=================================\n"
              << "              LIMIT ORDER BOOK  --  LATENCY BENCHMARKS\n"
              << "============================================================"
                 "=================================\n\n";

    // ---- Timer overhead baseline -------------------------------------------
    LatencyStats overhead = measureTimerOverhead(200000);
    g_overheadP50 = overhead.p50_ns;

    std::cout << "TIMER OVERHEAD BASELINE (empty loop, two back-to-back "
                 "steady_clock::now())\n";
    std::cout << "  p50 = " << std::fixed << std::setprecision(1) << overhead.p50_ns
              << " ns   mean = " << overhead.mean_ns
              << " ns   min = " << overhead.min_ns << " ns\n";
    std::cout << "  Every timed sample below contains this cost. The 'p50-ovh' column\n"
                 "  is the p50 with this floor SUBTRACTED. Rows marked '<floor' are at\n"
                 "  or below the resolution of the timer and cannot be resolved by this\n"
                 "  harness -- they are NOT evidence of a sub-nanosecond operation.\n\n";

    // ---- The O(1) cancel experiment ----------------------------------------
    std::cout << ">> CANCEL vs PRICE LEVELS M  (order count N fixed at " << kIters
              << ")\n";
    std::cout << "   O(1) in M predicts a FLAT column. An O(log M) map::find would "
                 "climb.\n\n";
    printHeader("Cancel");
    for (size_t m : kLevels) {
        (void)benchCancelVsLevels(m, kWarmup);  // warmup: same code path, discarded
        printRow(levelLabel("cancel", m), benchCancelVsLevels(m, kIters));
    }
    std::cout << "\n";

    std::cout << ">> CANCEL vs ORDER COUNT N  (price levels M fixed at 100)\n";
    std::cout << "   O(1) in N predicts a FLAT column.\n\n";
    printHeader("Cancel");
    for (size_t n : {10000u, 50000u, 200000u}) {
        (void)benchCancelVsOrders(kWarmup, 100);
        printRow(std::string("cancel N=") + std::to_string(n),
                 benchCancelVsOrders(n, 100));
    }
    std::cout << "\n";

    // ---- The honest counter-example ----------------------------------------
    std::cout << ">> getVolumeAtPrice vs PRICE LEVELS M\n";
    std::cout << "   This is a std::map::find: genuinely O(log M). This column is "
                 "EXPECTED to climb.\n\n";
    printHeader("getVolumeAtPrice");
    for (size_t m : kLevels) {
        (void)benchVolumeVsLevels(m, kWarmup);
        printRow(levelLabel("volume", m), benchVolumeVsLevels(m, kIters));
    }
    std::cout << "\n";

    // ---- Add ---------------------------------------------------------------
    std::cout << ">> ADD resting limit order vs PRICE LEVELS M\n\n";
    printHeader("Add limit (no match)");
    for (size_t m : kLevels) {
        (void)benchAddLimitVsLevels(m, kWarmup);
        printRow(levelLabel("add", m), benchAddLimitVsLevels(m, kIters));
    }
    std::cout << "\n";

    // ---- Modify: the two paths --------------------------------------------
    std::cout << ">> MODIFY: in-place shrink vs full requeue  (M = 100)\n";
    std::cout << "   In-place keeps queue priority and moves nothing. Requeue is a\n"
                 "   remove + re-add. The old code charged the requeue cost -- and the\n"
                 "   lost queue position -- on EVERY amend, including pure shrinks.\n\n";
    printHeader("Modify");
    (void)benchModifyInPlace(100, kWarmup);
    printRow("modify in-place (shrink)", benchModifyInPlace(100, kIters));
    (void)benchModifyRequeue(100, kWarmup);
    printRow("modify requeue (reprice)", benchModifyRequeue(100, kIters));
    std::cout << "\n";

    // ---- Queries and everything else ---------------------------------------
    std::cout << ">> OTHER OPERATIONS\n\n";
    printHeader("Operation");
    (void)benchGetBestBidAsk(1000, kWarmup);
    printRow("getBestBid+getBestAsk", benchGetBestBidAsk(1000, kIters));

    (void)benchMarketOrder(5, 500);
    printRow("market order (5 levels)", benchMarketOrder(5, 5000));
    (void)benchMarketOrder(50, 200);
    printRow("market order (50 levels)", benchMarketOrder(50, 2000));

    (void)benchMixedWorkload(1000, kWarmup);
    printRow("mixed 50/30/20 (10k book)", benchMixedWorkload(10000, kIters));
    std::cout << "\n";

    std::cout << "============================================================"
                 "=================================\n"
              << "  All figures in nanoseconds, -O3. Measured on whatever machine "
                 "this ran on --\n"
              << "  read the SHAPE of each column, not the absolute numbers.\n"
              << "============================================================"
                 "=================================\n\n";

    return 0;
}
