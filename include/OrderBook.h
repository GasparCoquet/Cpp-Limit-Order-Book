#pragma once

#include "Order.h"
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>

namespace LOB {

// A price level contains a FIFO queue of orders
struct PriceLevel {
    Price price;
    std::list<Order> orders;
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
    
    // O(1) order lookup: maps OrderId -> (iterator to order in list, price level)
    struct OrderLocation {
        std::list<Order>::iterator orderIter;
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
