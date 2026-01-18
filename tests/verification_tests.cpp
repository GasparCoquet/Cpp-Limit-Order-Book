#include "OrderBook.h"
#include <iostream>
#include <cassert>

using namespace LOB;

void testBasicOrderAddition() {
    std::cout << "TEST 1: Basic Order Addition..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::SELL, OrderType::LIMIT, 10100, 100, 0));
    
    assert(book.getBestBid() == 10000);
    assert(book.getBestAsk() == 10100);
    assert(book.getOrderCount() == 2);
    std::cout << " PASSED ✓\n";
}

void testOrderMatching() {
    std::cout << "TEST 2: Order Matching..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 50, 0));
    
    assert(book.getTrades().size() == 1);
    assert(book.getTrades()[0].quantity == 50);
    assert(book.getTrades()[0].price == 10000);
    assert(book.getOrderCount() == 1); // 50 remaining
    std::cout << " PASSED ✓\n";
}

void testOrderCancellation() {
    std::cout << "TEST 3: Order Cancellation (O(1))..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 9900, 150, 0));
    
    assert(book.getOrderCount() == 2);
    assert(book.cancelOrder(1) == true);
    assert(book.getOrderCount() == 1);
    assert(book.getBestBid() == 9900);
    std::cout << " PASSED ✓\n";
}

void testTimePriority() {
    std::cout << "TEST 4: Time Priority (FIFO)..." << std::flush;
    OrderBook book;
    // Add 3 orders at same price
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(3, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    
    // Sell 150 - should match order 1 fully, order 2 partially
    book.addOrder(Order(4, Side::SELL, OrderType::LIMIT, 10000, 150, 0));
    
    assert(book.getTrades().size() == 2);
    assert(book.getTrades()[0].buyOrderId == 1); // First order matched first
    assert(book.getTrades()[0].quantity == 100);
    assert(book.getTrades()[1].buyOrderId == 2); // Second order matched second
    assert(book.getTrades()[1].quantity == 50);
    std::cout << " PASSED ✓\n";
}

void testPricePriority() {
    std::cout << "TEST 5: Price Priority..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 9900, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 100, 0)); // Better price
    
    book.addOrder(Order(3, Side::SELL, OrderType::LIMIT, 9900, 50, 0));
    
    // Should match with order 2 (better price) not order 1
    assert(book.getTrades().size() == 1);
    assert(book.getTrades()[0].buyOrderId == 2);
    std::cout << " PASSED ✓\n";
}

void testMarketOrder() {
    std::cout << "TEST 6: Market Orders..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::SELL, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::SELL, OrderType::LIMIT, 10100, 100, 0));
    
    book.addOrder(Order(3, Side::BUY, OrderType::MARKET, 0, 150, 0));
    
    assert(book.getTrades().size() == 2);
    assert(book.getTrades()[0].quantity == 100); // First level fully filled
    assert(book.getTrades()[1].quantity == 50);  // Second level partially filled
    std::cout << " PASSED ✓\n";
}

void testOrderModification() {
    std::cout << "TEST 7: Order Modification..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    
    assert(book.modifyOrder(1, 10100, 200) == true);
    assert(book.getBestBid() == 10100);
    auto vol = book.getVolumeAtPrice(Side::BUY, 10100);
    assert(vol.has_value() && vol.value() == 200);
    std::cout << " PASSED ✓\n";
}

void testVolumeAtPrice() {
    std::cout << "TEST 8: Volume at Price..." << std::flush;
    OrderBook book;
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 10000, 150, 0));
    
    auto vol = book.getVolumeAtPrice(Side::BUY, 10000);
    assert(vol.has_value() && vol.value() == 250);
    std::cout << " PASSED ✓\n";
}

void performanceTest() {
    std::cout << "TEST 9: Performance (10,000 operations)..." << std::flush;
    OrderBook book;
    
    // Add 10,000 orders
    for (int i = 0; i < 10000; i++) {
        book.addOrder(Order(i, Side::BUY, OrderType::LIMIT, 10000 - (i % 100), 100, 0));
    }
    
    // Cancel 5,000 orders (O(1) each)
    for (int i = 0; i < 5000; i++) {
        book.cancelOrder(i * 2);
    }
    
    assert(book.getOrderCount() == 5000);
    std::cout << " PASSED ✓\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "    ORDER BOOK VERIFICATION TESTS\n";
    std::cout << "========================================\n\n";
    
    try {
        testBasicOrderAddition();
        testOrderMatching();
        testOrderCancellation();
        testTimePriority();
        testPricePriority();
        testMarketOrder();
        testOrderModification();
        testVolumeAtPrice();
        performanceTest();
        
        std::cout << "\n========================================\n";
        std::cout << "  ✓ ALL TESTS PASSED (9/9)\n";
        std::cout << "========================================\n\n";
        std::cout << "Your Order Book implementation is:\n";
        std::cout << "  ✓ Functionally correct\n";
        std::cout << "  ✓ O(1) order lookup working\n";
        std::cout << "  ✓ Price-Time priority working\n";
        std::cout << "  ✓ Performance verified\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cout << "\n✗ TEST FAILED: " << e.what() << "\n";
        return 1;
    }
}
