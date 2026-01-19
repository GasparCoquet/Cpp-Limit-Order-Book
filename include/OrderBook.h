#pragma once

#include "Order.h"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

namespace LOB {

// A price level contains a FIFO queue of orders
// Using deque for better cache locality than list
struct PriceLevel {
    Price price;
    std::deque<Order> orders;  // Better cache locality than std::list
    Quantity totalQuantity;

    PriceLevel(Price p) : price(p), totalQuantity(0) {}
};

class OrderBook {
public:
    OrderBook();
    ~OrderBook() = default;

    // Core operations
    void addOrder(const Order& order);
    bool cancelOrder(OrderId orderId);
    bool modifyOrder(OrderId orderId, Price newPrice, Quantity newQuantity);
    
    // Market data queries
    std::optional<Price> getBestBid() const;
    std::optional<Price> getBestAsk() const;
    std::optional<Quantity> getVolumeAtPrice(Side side, Price price) const;
    
    // Trade history
    const std::vector<Trade>& getTrades() const { return trades_; }
    
    // Statistics
    size_t getOrderCount() const { return orderIndex_.size(); }
    void printBook(int depth = 10) const;

private:
    // Dual-structure approach for O(1) lookup + O(log N) ordered access
    // Bids: Higher price has priority (descending order)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // Asks: Lower price has priority (ascending order)
    std::map<Price, PriceLevel, std::less<Price>> asks_;
    
    // O(1) order lookup: maps OrderId -> (index in deque, price level, side)
    // Using indices instead of iterators to avoid invalidation issues
    struct OrderLocation {
        size_t orderIndex;  // Index in the deque at this price level
        Price priceLevel;
        Side side;
    };
    std::unordered_map<OrderId, OrderLocation> orderIndex_;
    
    // Trade execution history
    std::vector<Trade> trades_;
    
    // Timestamp counter for order priority
    uint64_t timestamp_;

    // Internal matching engine
    void matchLimitOrder(Order& order);
    void matchMarketOrder(Order& order);
    void executeTrade(Order& aggressor, Order& resting, Quantity quantity);
    
    // Helper methods
    void addToBook(Order order);
    void removeFromBook(OrderId orderId);
    std::map<Price, PriceLevel, std::greater<Price>>& getBidBook() { return bids_; }
    std::map<Price, PriceLevel, std::less<Price>>& getAskBook() { return asks_; }
    
    template<typename Comparator>
    void matchAgainstBook(Order& order, std::map<Price, PriceLevel, Comparator>& book, 
                          bool (*canMatch)(Price, Price));
};

} // namespace LOB
