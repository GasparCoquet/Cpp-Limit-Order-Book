#include "OrderBook.h"

#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <vector>

using namespace LOB;

namespace {

int g_checksRun = 0;
int g_checksFailed = 0;
int g_failuresInCurrentTest = 0;

// A test assertion that SURVIVES NDEBUG.
//
// This suite used to use bare assert(). CMAKE_BUILD_TYPE=Release defines NDEBUG,
// and <cassert> expands assert() to ((void)0) under NDEBUG -- so in the Release
// build that this project actually ships, every single assertion compiled to
// nothing. Nothing was checked, no failure could ever be detected, and the binary
// still printed "ALL TESTS PASSED (9/9)" and exited 0. The green result was an
// artifact of the preprocessor, not evidence of correctness.
//
// CHECK is an ordinary runtime branch. It cannot be compiled away, it names the
// failing expression and its source location, and it drives a nonzero exit code.
// Dependency-free by design -- no GoogleTest, no Catch2.
#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checksRun;                                                          \
        if (!(cond)) {                                                          \
            ++g_checksFailed;                                                   \
            ++g_failuresInCurrentTest;                                          \
            std::fprintf(stderr, "\n    CHECK FAILED: %s\n      at %s:%d\n",    \
                         #cond, __FILE__, __LINE__);                            \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// Core behaviour
// ---------------------------------------------------------------------------

void testBasicOrderAddition() {
    OrderBook book;
    CHECK(book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0)));
    CHECK(book.addOrder(Order(2, Side::SELL, OrderType::LIMIT, 10100, 100, 0)));

    CHECK(book.getBestBid() == 10000);
    CHECK(book.getBestAsk() == 10100);
    CHECK(book.getOrderCount() == 2);
}

void testOrderMatching() {
    OrderBook book;
    book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 50, 0));

    CHECK(book.getTrades().size() == 1);
    CHECK(book.getTrades()[0].quantity == 50);
    CHECK(book.getTrades()[0].price == 10000);
    CHECK(book.getOrderCount() == 1);  // 50 remaining on the sell side
    CHECK(book.getVolumeAtPrice(Side::SELL, 10000) == 50);
}

void testOrderCancellation() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 9900, 150, 0));

    CHECK(book.getOrderCount() == 2);
    CHECK(book.cancelOrder(1) == true);
    CHECK(book.getOrderCount() == 1);
    CHECK(book.getBestBid() == 9900);

    // Cancelling a vanished id must fail, not corrupt anything.
    CHECK(book.cancelOrder(1) == false);
    CHECK(book.cancelOrder(999) == false);
    CHECK(book.getOrderCount() == 1);
}

void testTimePriority() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(3, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    // Sell 150 -> fills order 1 fully, order 2 partially.
    book.addOrder(Order(4, Side::SELL, OrderType::LIMIT, 10000, 150, 0));

    CHECK(book.getTrades().size() == 2);
    CHECK(book.getTrades()[0].buyOrderId == 1);
    CHECK(book.getTrades()[0].quantity == 100);
    CHECK(book.getTrades()[1].buyOrderId == 2);
    CHECK(book.getTrades()[1].quantity == 50);
}

void testPricePriority() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 9900, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));  // better price

    book.addOrder(Order(3, Side::SELL, OrderType::LIMIT, 9900, 50, 0));

    CHECK(book.getTrades().size() == 1);
    CHECK(book.getTrades()[0].buyOrderId == 2);  // best price wins
}

void testMarketOrder() {
    OrderBook book;
    book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::SELL, OrderType::LIMIT, 10100, 100, 0));

    book.addOrder(Order(3, Side::BUY, OrderType::MARKET, 0, 150, 0));

    CHECK(book.getTrades().size() == 2);
    CHECK(book.getTrades()[0].quantity == 100);
    CHECK(book.getTrades()[1].quantity == 50);
    CHECK(book.getBestAsk() == 10100);
}

void testVolumeAtPrice() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 150, 0));

    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 250);
    CHECK(book.getVolumeAtPrice(Side::BUY, 12345) == std::nullopt);
    CHECK(book.getVolumeAtPrice(Side::SELL, 10000) == std::nullopt);
}

// ---------------------------------------------------------------------------
// Amend semantics under price-time priority  (regression tests for modifyOrder)
// ---------------------------------------------------------------------------

// THE key regression test. modifyOrder used to unconditionally remove the order
// and re-add it with a fresh timestamp, so merely SHRINKING an order sent it to
// the back of the queue. Under price-time priority a size reduction only ever
// gives up liquidity, so it must RETAIN queue position.
void testModifyDecreasePreservesQueuePriority() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(3, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    // Shrink the FRONT order, 100 -> 40, same price.
    CHECK(book.modifyOrder(1, 10000, 40) == true);

    // Aggregate level volume must track the reduction: 40 + 100 + 100.
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 240);
    CHECK(book.getOrderCount() == 3);

    // Sell 60. Order 1 must STILL be at the head of the queue: it fills its
    // remaining 40 first, and order 2 supplies the last 20.
    // Under the old code order 1 had been requeued behind 2 and 3, so trade[0]
    // would have come from order 2 -- a real loss of queue position for a trader
    // who only reduced size.
    book.addOrder(Order(4, Side::SELL, OrderType::LIMIT, 10000, 60, 0));

    CHECK(book.getTrades().size() == 2);
    CHECK(book.getTrades()[0].buyOrderId == 1);
    CHECK(book.getTrades()[0].quantity == 40);
    CHECK(book.getTrades()[1].buyOrderId == 2);
    CHECK(book.getTrades()[1].quantity == 20);
}

// A size INCREASE is a request for more fill, so it correctly forfeits priority.
void testModifyIncreaseLosesQueuePriority() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    CHECK(book.modifyOrder(1, 10000, 150) == true);  // size up -> to the back
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 250);

    // Order 2 is now at the front.
    book.addOrder(Order(3, Side::SELL, OrderType::LIMIT, 10000, 100, 0));

    CHECK(book.getTrades().size() == 1);
    CHECK(book.getTrades()[0].buyOrderId == 2);
    CHECK(book.getTrades()[0].quantity == 100);
}

// An amend that leaves size unchanged is still an in-place no-op: priority kept.
void testModifySameQuantityKeepsPriority() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    CHECK(book.modifyOrder(1, 10000, 100) == true);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 200);

    book.addOrder(Order(3, Side::SELL, OrderType::LIMIT, 10000, 100, 0));
    CHECK(book.getTrades().size() == 1);
    CHECK(book.getTrades()[0].buyOrderId == 1);  // still first
}

// A price change legitimately forfeits priority and relocates the order.
void testModifyPriceChangeRequeues() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    CHECK(book.modifyOrder(1, 10100, 200) == true);
    CHECK(book.getBestBid() == 10100);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10100) == 200);
    // The vacated level must be gone, not left behind as a phantom.
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == std::nullopt);
    CHECK(book.getOrderCount() == 1);
}

void testModifyToZeroCancels() {
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));

    CHECK(book.modifyOrder(1, 10000, 0) == true);
    CHECK(book.getOrderCount() == 0);
    CHECK(book.getBestBid() == std::nullopt);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == std::nullopt);
}

void testModifyUnknownOrderFails() {
    OrderBook book;
    CHECK(book.modifyOrder(42, 10000, 100) == false);
    CHECK(book.getOrderCount() == 0);
}

// ---------------------------------------------------------------------------
// Duplicate order ids
// ---------------------------------------------------------------------------

// addToBook wrote the index with `orderIndex_[id] = ...`, which silently
// OVERWRITES on a repeated id. The first order stayed in its price level -- still
// counted in totalQuantity, still matchable -- but its only handle was gone, so it
// could never be cancelled or modified again. Live risk, permanently stranded.
void testDuplicateOrderIdRejected() {
    OrderBook book;
    CHECK(book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0)) == true);

    // Re-using a live id must be REJECTED outright.
    CHECK(book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 9900, 250, 0)) == false);

    // ...and must leave the book completely untouched.
    CHECK(book.getOrderCount() == 1);
    CHECK(book.getBestBid() == 10000);
    CHECK(book.getVolumeAtPrice(Side::BUY, 9900) == std::nullopt);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 100);

    // The orphan check: cancelling the id must empty the book entirely. Under the
    // old operator[] write this failed -- 100 lots of phantom liquidity survived at
    // 10000 with no way to ever remove them.
    CHECK(book.cancelOrder(1) == true);
    CHECK(book.getOrderCount() == 0);
    CHECK(book.getBestBid() == std::nullopt);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == std::nullopt);
}

// An id is only "live" while it rests in the book. Once it is gone, it is reusable.
void testOrderIdReusableAfterCancel() {
    OrderBook book;
    CHECK(book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0)) == true);
    CHECK(book.cancelOrder(1) == true);
    CHECK(book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 9900, 70, 0)) == true);
    CHECK(book.getOrderCount() == 1);
    CHECK(book.getVolumeAtPrice(Side::BUY, 9900) == 70);
}

// A fully-filled order leaves the index, so its id frees up too.
void testOrderIdReusableAfterFullFill() {
    OrderBook book;
    CHECK(book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10000, 100, 0)) == true);
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));  // fills id 1
    CHECK(book.getOrderCount() == 0);
    CHECK(book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10500, 30, 0)) == true);
    CHECK(book.getVolumeAtPrice(Side::SELL, 10500) == 30);
}

// ---------------------------------------------------------------------------
// O(1) cancel: cached price-level iterator must survive book churn
// ---------------------------------------------------------------------------

// OrderLocation now caches the std::map iterator for the order's price level
// instead of its Price, which is what removes the O(log M) map::find from cancel.
// That is only sound because std::map is node-based: its iterators stay valid
// across inserts and across erasure of OTHER nodes. This test hammers exactly that
// invariant -- if the cached iterator were ever invalidated, these cancels would
// read freed memory rather than return cleanly.
void testCachedLevelIteratorSurvivesChurn() {
    OrderBook book;
    const int levels = 200;

    // One resting bid per price level.
    for (int i = 0; i < levels; ++i) {
        CHECK(book.addOrder(Order(static_cast<OrderId>(i + 1), Side::BUY,
                                  OrderType::LIMIT, 10000 - i, 100, 0)) == true);
    }
    CHECK(book.getOrderCount() == static_cast<size_t>(levels));

    // Insert a second band of levels. Every one of these is a fresh map node
    // inserted below the existing ones; the cached iterators must all survive.
    for (int i = 0; i < levels; ++i) {
        CHECK(book.addOrder(Order(static_cast<OrderId>(1000 + i), Side::BUY,
                                  OrderType::LIMIT, 5000 - i, 50, 0)) == true);
    }
    CHECK(book.getOrderCount() == static_cast<size_t>(2 * levels));

    // Erase every other level from the first band. Each cancel empties its level
    // and erases that map node -- which must not disturb any other cached iterator.
    for (int i = 0; i < levels; i += 2) {
        CHECK(book.cancelOrder(static_cast<OrderId>(i + 1)) == true);
        CHECK(book.getVolumeAtPrice(Side::BUY, 10000 - i) == std::nullopt);
    }

    // Every surviving order must still be reachable through its cached iterator.
    for (int i = 1; i < levels; i += 2) {
        CHECK(book.getVolumeAtPrice(Side::BUY, 10000 - i) == 100);
        CHECK(book.cancelOrder(static_cast<OrderId>(i + 1)) == true);
    }
    for (int i = 0; i < levels; ++i) {
        CHECK(book.cancelOrder(static_cast<OrderId>(1000 + i)) == true);
    }

    CHECK(book.getOrderCount() == 0);
    CHECK(book.getBestBid() == std::nullopt);
}

// Cancels must work identically on the ask side (the other map type).
void testCancelOnAskSide() {
    OrderBook book;
    book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10100, 100, 0));
    book.addOrder(Order(2, Side::SELL, OrderType::LIMIT, 10200, 150, 0));
    book.addOrder(Order(3, Side::SELL, OrderType::LIMIT, 10100, 25, 0));

    CHECK(book.getVolumeAtPrice(Side::SELL, 10100) == 125);
    CHECK(book.cancelOrder(1) == true);
    CHECK(book.getVolumeAtPrice(Side::SELL, 10100) == 25);  // level survives
    CHECK(book.getBestAsk() == 10100);

    CHECK(book.cancelOrder(3) == true);
    CHECK(book.getVolumeAtPrice(Side::SELL, 10100) == std::nullopt);  // level gone
    CHECK(book.getBestAsk() == 10200);
    CHECK(book.getOrderCount() == 1);
}

// ---------------------------------------------------------------------------
// Bulk consistency
// ---------------------------------------------------------------------------

void testBulkAddCancelConsistency() {
    OrderBook book;

    for (int i = 0; i < 10000; i++) {
        CHECK(book.addOrder(Order(static_cast<OrderId>(i), Side::BUY, OrderType::LIMIT,
                                  10000 - (i % 100), 100, 0)) == true);
    }
    CHECK(book.getOrderCount() == 10000);

    // Every price level holds 100 orders of 100 lots.
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == 100 * 100);

    for (int i = 0; i < 5000; i++) {
        CHECK(book.cancelOrder(static_cast<OrderId>(i * 2)) == true);
    }
    CHECK(book.getOrderCount() == 5000);

    // Cancel the rest; the book must end up completely empty with no stale levels.
    for (int i = 0; i < 5000; i++) {
        CHECK(book.cancelOrder(static_cast<OrderId>(i * 2 + 1)) == true);
    }
    CHECK(book.getOrderCount() == 0);
    CHECK(book.getBestBid() == std::nullopt);
    CHECK(book.getVolumeAtPrice(Side::BUY, 10000) == std::nullopt);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

struct TestCase {
    const char* name;
    void (*fn)();
};

const TestCase kTests[] = {
    {"Basic order addition",                     testBasicOrderAddition},
    {"Order matching",                           testOrderMatching},
    {"Order cancellation",                       testOrderCancellation},
    {"Time priority (FIFO)",                     testTimePriority},
    {"Price priority",                           testPricePriority},
    {"Market orders",                            testMarketOrder},
    {"Volume at price",                          testVolumeAtPrice},
    {"Modify: decrease PRESERVES queue priority", testModifyDecreasePreservesQueuePriority},
    {"Modify: increase LOSES queue priority",    testModifyIncreaseLosesQueuePriority},
    {"Modify: same qty keeps priority",          testModifySameQuantityKeepsPriority},
    {"Modify: price change requeues",            testModifyPriceChangeRequeues},
    {"Modify: zero quantity cancels",            testModifyToZeroCancels},
    {"Modify: unknown id fails",                 testModifyUnknownOrderFails},
    {"Duplicate order id rejected",              testDuplicateOrderIdRejected},
    {"Order id reusable after cancel",           testOrderIdReusableAfterCancel},
    {"Order id reusable after full fill",        testOrderIdReusableAfterFullFill},
    {"Cached level iterator survives churn",     testCachedLevelIteratorSurvivesChurn},
    {"Cancel on ask side",                       testCancelOnAskSide},
    {"Bulk add/cancel consistency",              testBulkAddCancelConsistency},
};

}  // namespace

int main() {
    const int total = static_cast<int>(sizeof(kTests) / sizeof(kTests[0]));
    int passed = 0;
    int failed = 0;

    std::cout << "\n========================================\n";
    std::cout << "    ORDER BOOK VERIFICATION TESTS\n";
    std::cout << "========================================\n\n";

    for (int i = 0; i < total; ++i) {
        std::cout << "TEST " << (i + 1) << ": " << kTests[i].name << "..." << std::flush;
        g_failuresInCurrentTest = 0;

        try {
            kTests[i].fn();
        } catch (const std::exception& e) {
            ++g_failuresInCurrentTest;
            ++g_checksFailed;
            std::fprintf(stderr, "\n    THREW: %s\n", e.what());
        } catch (...) {
            ++g_failuresInCurrentTest;
            ++g_checksFailed;
            std::fprintf(stderr, "\n    THREW: unknown exception\n");
        }

        if (g_failuresInCurrentTest == 0) {
            std::cout << " PASSED\n";
            ++passed;
        } else {
            std::cout << "  -> FAILED (" << g_failuresInCurrentTest << " check(s))\n";
            ++failed;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "  Tests:  " << passed << " passed, " << failed << " failed, "
              << total << " total\n";
    std::cout << "  Checks: " << (g_checksRun - g_checksFailed) << " passed, "
              << g_checksFailed << " failed, " << g_checksRun << " total\n";
    std::cout << "========================================\n";

    // The check count is printed from a counter that CHECK increments at runtime.
    // If NDEBUG had silently removed the assertions (the old bug), this number
    // would read 0 and the discrepancy would be obvious rather than invisible.
    if (g_checksRun == 0) {
        std::fprintf(stderr, "\nFATAL: no checks executed -- assertions were compiled out.\n");
        return 2;
    }

    if (failed != 0 || g_checksFailed != 0) {
        std::cout << "\n  RESULT: FAILED\n\n";
        return 1;
    }

    std::cout << "\n  RESULT: ALL TESTS PASSED\n\n";
    return 0;
}
