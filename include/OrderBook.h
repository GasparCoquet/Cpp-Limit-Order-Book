#pragma once

#include "Order.h"
#include <list>
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

namespace LOB {

// A price level contains a FIFO queue of orders (doubly linked list for O(1) erase)
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

    // Core operations.
    // addOrder returns false if the order is rejected (currently: an OrderId that is
    // already resting in the book). cancel/modify return false on an unknown id.
    bool addOrder(const Order& order);
    bool cancelOrder(OrderId orderId);

    // Amend, with price-time priority semantics:
    //   newQuantity == 0              -> treated as a cancel
    //   same price, size decrease     -> amended IN PLACE, queue position RETAINED
    //   price change or size increase -> re-queued at the BACK of the new level
    bool modifyOrder(OrderId orderId, Price newPrice, Quantity newQuantity);

    // Market data queries
    std::optional<Price> getBestBid() const;
    std::optional<Price> getBestAsk() const;
    std::optional<Quantity> getVolumeAtPrice(Side side, Price price) const;

    // Trade history
    const std::vector<Trade>& getTrades() const { return trades_; }

    // Statistics
    size_t getOrderCount() const { return bidIndex_.size() + askIndex_.size(); }
    void printBook(int depth = 10) const;

private:
    // Bids: higher price has priority (descending). Asks: lower price first (ascending).
    using BidBook = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskBook = std::map<Price, PriceLevel, std::less<Price>>;

    BidBook bids_;
    AskBook asks_;

    // O(1) order lookup.
    //
    // A location caches the std::map iterator for the order's price level, NOT its
    // Price. std::map is node-based: iterators stay valid across inserts and across
    // erasure of *other* nodes, so an iterator captured at insert time is good for
    // the whole life of the order. This is what makes cancel genuinely O(1) --
    // removeFromBook never has to re-locate the level with an O(log M) map::find.
    // The index probe is O(1), list::erase(iterator) is O(1), and
    // map::erase(iterator) is amortized O(1). No search anywhere on the path.
    template <typename LevelIt>
    struct Location {
        std::list<Order>::iterator orderIt;  // the order's node in the level's FIFO
        LevelIt levelIt;                     // the level's node in the price map
    };
    using BidLocation = Location<BidBook::iterator>;
    using AskLocation = Location<AskBook::iterator>;

    // The index is split by side because each side's location holds a *differently
    // typed* map iterator. (A std::variant of the two is not an option: the
    // comparator is not part of a map iterator's type, so on libstdc++ both sides
    // name the same _Rb_tree_iterator and the variant would have duplicate
    // alternatives -- and the standard guarantees nothing either way. Splitting the
    // index sidesteps the question entirely and stays type-safe.)
    // A lookup by id alone probes bidIndex_ then askIndex_: two O(1) hash probes.
    std::unordered_map<OrderId, BidLocation> bidIndex_;
    std::unordered_map<OrderId, AskLocation> askIndex_;

    // Trade execution history
    std::vector<Trade> trades_;

    // Timestamp counter for order priority
    uint64_t timestamp_;

    // Internal matching engine
    void matchLimitOrder(Order& order);
    void matchMarketOrder(Order& order);
    void executeTrade(Order& aggressor, Order& resting, Quantity quantity);

    // Helper methods
    void addToBook(const Order& order);
    void removeFromBook(OrderId orderId);

    // True if the id is currently resting in the book (either side).
    bool isLive(OrderId orderId) const;

    // Locate a resting order without any map search. Returns nullptr if unknown.
    // On success *outLevel (when non-null) is the level the order sits in.
    Order* findResting(OrderId orderId, PriceLevel** outLevel);

    // Drop an id from whichever index owns it.
    void eraseIndex(Side side, OrderId orderId);

    // Erase an order from its level via the cached iterators, dropping the level if
    // it just emptied. Defined in the .cpp; only ever instantiated there.
    template <typename Book, typename Loc>
    static void eraseOrderFromLevel(Book& book, const Loc& loc);

    template<typename Comparator>
    void matchAgainstBook(Order& order, std::map<Price, PriceLevel, Comparator>& book,
                          bool (*canMatch)(Price, Price));
};

} // namespace LOB
