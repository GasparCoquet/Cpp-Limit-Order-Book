#include "OrderBook.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace LOB {

OrderBook::OrderBook() : timestamp_(0) {}

bool OrderBook::addOrder(const Order& order) {
    switch (order.type) {
        case OrderType::CANCEL:
            return cancelOrder(order.id);
        case OrderType::MODIFY:
            return modifyOrder(order.id, order.price, order.quantity);
        case OrderType::LIMIT:
        case OrderType::MARKET:
            break;
    }

    // Reject duplicate live OrderIds.
    //
    // addToBook wrote the index with `orderIndex_[id] = ...`, which silently
    // OVERWRITES the entry when an id is reused. The first order then stayed in its
    // price level's list -- still counted in totalQuantity, still matchable against
    // incoming flow -- while the only handle to it was destroyed. It could never be
    // cancelled or amended again: live risk, stranded in the book forever, and the
    // book's own order count under-reported it. Refuse the duplicate instead.
    if (isLive(order.id)) {
        return false;
    }

    Order newOrder = order;
    newOrder.timestamp = timestamp_++;

    if (newOrder.type == OrderType::LIMIT) {
        matchLimitOrder(newOrder);
    } else {
        matchMarketOrder(newOrder);
    }
    return true;
}

bool OrderBook::cancelOrder(OrderId orderId) {
    if (!isLive(orderId)) {
        return false;  // Order not resting in the book
    }

    removeFromBook(orderId);
    return true;
}

bool OrderBook::modifyOrder(OrderId orderId, Price newPrice, Quantity newQuantity) {
    PriceLevel* level = nullptr;
    Order* resting = findResting(orderId, &level);
    if (resting == nullptr) {
        return false;  // Order not found
    }

    // An amend down to zero size is a cancel.
    if (newQuantity == 0) {
        removeFromBook(orderId);
        return true;
    }

    // Under price-time priority a pure size REDUCTION must RETAIN queue position:
    // the trader is only giving up liquidity, never asking for a better place in
    // line. Only a price change or a size INCREASE forfeits time priority.
    //
    // The old code removed and re-added the order with a fresh timestamp on EVERY
    // amend, so merely shrinking an order silently sent it to the back of the queue
    // -- a real, unearned economic loss for the trader, and a violation of the very
    // matching rule this book claims to implement.
    if (newPrice == resting->price && newQuantity <= resting->quantity) {
        level->totalQuantity -= (resting->quantity - newQuantity);
        resting->quantity = newQuantity;
        // timestamp deliberately untouched -> queue position retained.
        return true;
    }

    // Price change or size increase: forfeit priority, re-enter at the back.
    Order updated = *resting;  // copy before removeFromBook invalidates the node
    updated.price = newPrice;
    updated.quantity = newQuantity;
    updated.type = OrderType::LIMIT;  // re-enter as a limit order, never as MODIFY

    removeFromBook(orderId);  // must precede addOrder: frees the id for reuse
    addOrder(updated);        // fresh timestamp; may now cross and match
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
                // Remove fully filled order. Its id leaves the index, so it becomes
                // reusable; the level itself is erased below once it empties.
                eraseIndex(restingOrder.side, restingOrder.id);
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

bool OrderBook::isLive(OrderId orderId) const {
    return bidIndex_.find(orderId) != bidIndex_.end() ||
           askIndex_.find(orderId) != askIndex_.end();
}

void OrderBook::eraseIndex(Side side, OrderId orderId) {
    if (side == Side::BUY) {
        bidIndex_.erase(orderId);
    } else {
        askIndex_.erase(orderId);
    }
}

Order* OrderBook::findResting(OrderId orderId, PriceLevel** outLevel) {
    // Both branches are O(1) hash probes, and the cached level iterator means we
    // reach the PriceLevel with a pointer hop -- never a map search.
    auto bidIt = bidIndex_.find(orderId);
    if (bidIt != bidIndex_.end()) {
        if (outLevel) *outLevel = &bidIt->second.levelIt->second;
        return &(*bidIt->second.orderIt);
    }

    auto askIt = askIndex_.find(orderId);
    if (askIt != askIndex_.end()) {
        if (outLevel) *outLevel = &askIt->second.levelIt->second;
        return &(*askIt->second.orderIt);
    }

    return nullptr;
}

// Erase one order from its level, and drop the level if it just went empty.
// list::erase(iterator) is O(1); map::erase(iterator) is amortized O(1).
template <typename Book, typename Loc>
void OrderBook::eraseOrderFromLevel(Book& book, const Loc& loc) {
    auto levelIt = loc.levelIt;
    PriceLevel& level = levelIt->second;

    level.totalQuantity -= loc.orderIt->quantity;
    level.orders.erase(loc.orderIt);

    if (level.orders.empty()) {
        book.erase(levelIt);
    }
}

void OrderBook::addToBook(const Order& order) {
    // Defence in depth: addOrder already rejects duplicate ids. Never let an index
    // write clobber a live order -- that is exactly how orders used to get orphaned.
    if (isLive(order.id)) {
        return;
    }

    if (order.side == Side::BUY) {
        auto levelIt = bids_.try_emplace(order.price, order.price).first;
        PriceLevel& level = levelIt->second;

        level.orders.push_back(order);
        level.totalQuantity += order.quantity;
        // Cache BOTH iterators: the list node AND the map node.
        bidIndex_.emplace(order.id,
                          BidLocation{std::prev(level.orders.end()), levelIt});
    } else {
        auto levelIt = asks_.try_emplace(order.price, order.price).first;
        PriceLevel& level = levelIt->second;

        level.orders.push_back(order);
        level.totalQuantity += order.quantity;
        askIndex_.emplace(order.id,
                          AskLocation{std::prev(level.orders.end()), levelIt});
    }
}

void OrderBook::removeFromBook(OrderId orderId) {
    // There is no std::map::find anywhere on this path any more. The level iterator
    // was cached at insert time, so a cancel is: an O(1) hash probe, an O(1)
    // list::erase, and an amortized-O(1) map::erase. Cancel is now genuinely O(1) in
    // the order count N *and* in the price-level count M -- previously the O(log M)
    // map::find made the advertised O(1) false.
    auto bidIt = bidIndex_.find(orderId);
    if (bidIt != bidIndex_.end()) {
        eraseOrderFromLevel(bids_, bidIt->second);
        bidIndex_.erase(bidIt);
        return;
    }

    auto askIt = askIndex_.find(orderId);
    if (askIt != askIndex_.end()) {
        eraseOrderFromLevel(asks_, askIt->second);
        askIndex_.erase(askIt);
    }
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
