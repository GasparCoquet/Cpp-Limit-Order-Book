#include "OrderBook.h"
#include <iostream>
#include <iomanip>

using namespace LOB;

void printTrades(const std::vector<Trade>& trades) {
    if (trades.empty()) {
        std::cout << "No trades executed.\n";
        return;
    }
    
    std::cout << "\n================= TRADE HISTORY =================\n";
    std::cout << std::setw(12) << "Buy Order" 
              << std::setw(12) << "Sell Order"
              << std::setw(10) << "Price"
              << std::setw(10) << "Quantity" << "\n";
    std::cout << "-------------------------------------------------\n";
    
    for (const auto& trade : trades) {
        std::cout << std::setw(12) << trade.buyOrderId
                  << std::setw(12) << trade.sellOrderId
                  << std::setw(10) << trade.price
                  << std::setw(10) << trade.quantity << "\n";
    }
    std::cout << "=================================================\n\n";
}

int main() {
    std::cout << "=== High-Performance Limit Order Book Demo ===\n\n";
    
    OrderBook book;
    
    // Example 1: Add some limit orders to both sides
    std::cout << "1. Adding initial orders to the book...\n";
    book.addOrder(Order(1, Side::BUY, OrderType::LIMIT, 10000, 100, 0));   // Buy 100 @ 100.00
    book.addOrder(Order(2, Side::BUY, OrderType::LIMIT, 9950, 150, 0));    // Buy 150 @ 99.50
    book.addOrder(Order(3, Side::BUY, OrderType::LIMIT, 9900, 200, 0));    // Buy 200 @ 99.00
    
    book.addOrder(Order(4, Side::SELL, OrderType::LIMIT, 10050, 100, 0));  // Sell 100 @ 100.50
    book.addOrder(Order(5, Side::SELL, OrderType::LIMIT, 10100, 150, 0));  // Sell 150 @ 101.00
    book.addOrder(Order(6, Side::SELL, OrderType::LIMIT, 10150, 200, 0));  // Sell 200 @ 101.50
    
    book.printBook(5);
    
    // Example 2: Add an aggressive buy order that matches
    std::cout << "2. Adding aggressive buy order @ 100.50 (will match with best ask)...\n";
    book.addOrder(Order(7, Side::BUY, OrderType::LIMIT, 10050, 50, 0));
    
    book.printBook(5);
    printTrades(book.getTrades());
    
    // Example 3: Market order
    std::cout << "3. Executing market sell order for 175 shares...\n";
    book.addOrder(Order(8, Side::SELL, OrderType::MARKET, 0, 175, 0));
    
    book.printBook(5);
    printTrades(book.getTrades());
    
    // Example 4: Cancel an order
    std::cout << "4. Canceling order ID 2...\n";
    if (book.cancelOrder(2)) {
        std::cout << "Order 2 successfully cancelled.\n";
    }
    
    book.printBook(5);
    
    // Example 5: Modify an order
    std::cout << "5. Modifying order ID 3 (new price: 99.25, new qty: 300)...\n";
    book.modifyOrder(3, 9925, 300);
    
    book.printBook(5);
    
    // Example 6: Time priority demonstration
    std::cout << "6. Testing time priority (FIFO at same price)...\n";
    book.addOrder(Order(9, Side::BUY, OrderType::LIMIT, 10200, 100, 0));   // First at 102.00
    book.addOrder(Order(10, Side::BUY, OrderType::LIMIT, 10200, 100, 0));  // Second at 102.00
    book.addOrder(Order(11, Side::BUY, OrderType::LIMIT, 10200, 100, 0));  // Third at 102.00
    
    std::cout << "Added 3 buy orders @ 102.00\n";
    book.printBook(10);
    
    std::cout << "Now selling 150 @ 102.00 (should match first 2 orders)...\n";
    book.addOrder(Order(12, Side::SELL, OrderType::LIMIT, 10200, 150, 0));
    
    book.printBook(10);
    printTrades(book.getTrades());
    
    // Final statistics
    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "Total orders remaining: " << book.getOrderCount() << "\n";
    std::cout << "Total trades executed: " << book.getTrades().size() << "\n";
    
    if (auto bid = book.getBestBid()) {
        std::cout << "Best Bid: " << *bid / 100.0 << "\n";
    }
    if (auto ask = book.getBestAsk()) {
        std::cout << "Best Ask: " << *ask / 100.0 << "\n";
    }
    
    return 0;
}
