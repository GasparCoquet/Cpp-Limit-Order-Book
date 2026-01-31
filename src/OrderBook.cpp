#include "OrderBook.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace LOB {

OrderBook::OrderBook() : timestamp_(0) {}

void OrderBook::addOrder(const Order& order) {
    Order newOrder = order;
    newOrder.timestamp = timestamp_++;
    
    switch (order.type) {
        case OrderType::LIMIT:
            matchLimitOrder(newOrder);
            break;
        case OrderType::MARKET:
            matchMarketOrder(newOrder);
            break;
        case OrderType::CANCEL:
            cancelOrder(order.id);
            break;
        case OrderType::MODIFY:
            modifyOrder(order.id, order.price, order.quantity);
            break;
    }
}

bool OrderBook::cancelOrder(OrderId orderId) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) {
        return false;  // Order not found
    }
    
    removeFromBook(orderId);
    return true;
}

bool OrderBook::modifyOrder(OrderId orderId, Price newPrice, Quantity newQuantity) {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) {
        return false;  // Order not found
    }
    
    const OrderLocation& loc = it->second;
    Order oldOrder = *loc.orderIt;
    
    removeFromBook(orderId);

    Order newOrder = oldOrder;
    newOrder.price = newPrice;
    newOrder.quantity = newQuantity;
    newOrder.timestamp = timestamp_++;  // New timestamp (loses priority)
    
    addOrder(newOrder);
    return true;
}

void OrderBook::matchLimitOrder(Order& order) {
    if (order.side == Side::BUY) {
        // Match against asks (sell orders)
        matchAgainstBook(order, asks_, [](Price buyPrice, Price askPrice) {
            return buyPrice >= askPrice;  // Buy can match if price >= ask
        });
    } else {
        // Match against bids (buy orders)
        matchAgainstBook(order, bids_, [](Price sellPrice, Price bidPrice) {
            return sellPrice <= bidPrice;  // Sell can match if price <= bid
        });
    }
    
    // If there's remaining quantity, add to book
    if (order.quantity > 0) {
        addToBook(order);
    }
}

void OrderBook::matchMarketOrder(Order& order) {
    if (order.side == Side::BUY) {
        // Market buy: match against asks at any price
        matchAgainstBook(order, asks_, [](Price, Price) {
            return true;  // Always match
        });
    } else {
        // Market sell: match against bids at any price
        matchAgainstBook(order, bids_, [](Price, Price) {
            return true;  // Always match
        });
    }
    
    // Market orders that can't be fully filled are cancelled (not added to book)
}

template<typename Comparator>
void OrderBook::matchAgainstBook(Order& order, std::map<Price, PriceLevel, Comparator>& book,
                                  bool (*canMatch)(Price, Price)) {
    auto levelIt = book.begin();
    
    while (levelIt != book.end() && order.quantity > 0) {
        PriceLevel& level = levelIt->second;
        
        // Check if price can match
        if (!canMatch(order.price, level.price)) {
            break;  // No more matching possible
        }
        
        auto orderIt = level.orders.begin();
        while (orderIt != level.orders.end() && order.quantity > 0) {
            Order& restingOrder = *orderIt;
            Quantity matchQty = std::min(order.quantity, restingOrder.quantity);
            
            executeTrade(order, restingOrder, matchQty);
            
            order.quantity -= matchQty;
            restingOrder.quantity -= matchQty;
            level.totalQuantity -= matchQty;
            
            if (restingOrder.quantity == 0) {
                // Remove fully filled order
                orderIndex_.erase(restingOrder.id);
                orderIt = level.orders.erase(orderIt);
            } else {
                ++orderIt;
            }
        }
        
        // Remove empty price level
        if (level.orders.empty()) {
            levelIt = book.erase(levelIt);
        } else {
            ++levelIt;
        }
    }
}

void OrderBook::executeTrade(Order& aggressor, Order& resting, Quantity quantity) {
    Price tradePrice = resting.price;  // Resting order price has priority
    
    OrderId buyId = aggressor.side == Side::BUY ? aggressor.id : resting.id;
    OrderId sellId = aggressor.side == Side::SELL ? aggressor.id : resting.id;
    
    trades_.emplace_back(buyId, sellId, tradePrice, quantity, timestamp_);
    
    // Optional: Print trade for debugging
    // std::cout << "TRADE: " << quantity << " @ " << tradePrice << std::endl;
}

void OrderBook::addToBook(Order order) {
    if (order.side == Side::BUY) {
        auto [levelIt, inserted] = bids_.try_emplace(order.price, order.price);
        PriceLevel& level = levelIt->second;
        
        level.orders.push_back(order);
        level.totalQuantity += order.quantity;
        orderIndex_[order.id] = {std::prev(level.orders.end()), order.price, order.side};
    } else {
        auto [levelIt, inserted] = asks_.try_emplace(order.price, order.price);
        PriceLevel& level = levelIt->second;

        level.orders.push_back(order);
        level.totalQuantity += order.quantity;
        orderIndex_[order.id] = {std::prev(level.orders.end()), order.price, order.side};
    }
}

void OrderBook::removeFromBook(OrderId orderId) {
    auto indexIt = orderIndex_.find(orderId);
    if (indexIt == orderIndex_.end()) {
        return;
    }

    const OrderLocation& loc = indexIt->second;
    std::map<Price, PriceLevel, std::greater<Price>>* bidBook = &bids_;
    std::map<Price, PriceLevel, std::less<Price>>* askBook = &asks_;

    if (loc.side == Side::BUY) {
        auto levelIt = bidBook->find(loc.priceLevel);
        if (levelIt != bidBook->end()) {
            PriceLevel& level = levelIt->second;
            level.totalQuantity -= loc.orderIt->quantity;
            level.orders.erase(loc.orderIt);
            if (level.orders.empty()) {
                bidBook->erase(levelIt);
            }
        }
    } else {
        auto levelIt = askBook->find(loc.priceLevel);
        if (levelIt != askBook->end()) {
            PriceLevel& level = levelIt->second;
            level.totalQuantity -= loc.orderIt->quantity;
            level.orders.erase(loc.orderIt);
            if (level.orders.empty()) {
                askBook->erase(levelIt);
            }
        }
    }

    orderIndex_.erase(indexIt);
}

std::optional<Price> OrderBook::getBestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::getBestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<Quantity> OrderBook::getVolumeAtPrice(Side side, Price price) const {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            return std::nullopt;
        }
        return it->second.totalQuantity;
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) {
            return std::nullopt;
        }
        return it->second.totalQuantity;
    }
}

void OrderBook::printBook(int depth) const {
    std::cout << "\n==================== ORDER BOOK ====================\n";
    std::cout << std::setw(10) << "BIDS" << std::setw(15) << "Price" 
              << std::setw(15) << "ASKS" << "\n";
    std::cout << "----------------------------------------------------\n";
    
    auto bidIt = bids_.begin();
    auto askIt = asks_.begin();
    
    for (int i = 0; i < depth; ++i) {
        // Print bid side
        if (bidIt != bids_.end()) {
            std::cout << std::setw(10) << bidIt->second.totalQuantity 
                      << std::setw(15) << bidIt->first;
            ++bidIt;
        } else {
            std::cout << std::setw(25) << " ";
        }
        
        // Print ask side
        if (askIt != asks_.end()) {
            std::cout << std::setw(15) << askIt->second.totalQuantity << "\n";
            ++askIt;
        } else {
            std::cout << "\n";
        }
    }
    
    std::cout << "====================================================\n";
    std::cout << "Best Bid: ";
    if (auto bid = getBestBid()) {
        std::cout << *bid;
    } else {
        std::cout << "N/A";
    }
    std::cout << " | Best Ask: ";
    if (auto ask = getBestAsk()) {
        std::cout << *ask;
    } else {
        std::cout << "N/A";
    }
    std::cout << "\n";
    std::cout << "Total Orders: " << getOrderCount() << "\n";
    std::cout << "Total Trades: " << trades_.size() << "\n\n";
}

} // namespace LOB
