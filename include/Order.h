#pragma once

#include <cstdint>
#include <string>

namespace LOB {

using OrderId = uint64_t;
using Price = int64_t;  // Price in ticks (e.g., cents)
using Quantity = uint64_t;

enum class Side {
    BUY,
    SELL
};

enum class OrderType {
    LIMIT,
    MARKET,
    CANCEL,
    MODIFY
};

struct Order {
    OrderId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
    uint64_t timestamp;  // For time priority

    Order(OrderId id, Side side, OrderType type, Price price, Quantity quantity, uint64_t timestamp)
        : id(id), side(side), type(type), price(price), quantity(quantity), timestamp(timestamp) {}
};

// Trade execution record
struct Trade {
    OrderId buyOrderId;
    OrderId sellOrderId;
    Price price;
    Quantity quantity;
    uint64_t timestamp;

    Trade(OrderId buyId, OrderId sellId, Price p, Quantity q, uint64_t ts)
        : buyOrderId(buyId), sellOrderId(sellId), price(p), quantity(q), timestamp(ts) {}
};

} // namespace LOB
