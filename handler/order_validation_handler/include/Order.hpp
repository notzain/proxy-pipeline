#pragma once

#include "core/serialization/TypeTraits.hpp"
#include <cstdint>
#include <string>

namespace Proxy::Model {

/**
 * @brief Domain model representing an order payload.
 */
struct Order {
    std::string customer;
    std::string item;
    std::uint32_t quantity { 0 };

    bool operator==(const Order& other) const noexcept
    {
        return customer == other.customer && item == other.item && quantity == other.quantity;
    }
    bool operator!=(const Order& other) const noexcept
    {
        return !(*this == other);
    }
};

/**
 * @brief Non-owning zero-copy domain model representing an order payload.
 */
struct OrderView {
    std::string_view customer;
    std::string_view item;
    std::uint32_t quantity { 0 };

    bool operator==(const OrderView& other) const noexcept
    {
        return customer == other.customer && item == other.item && quantity == other.quantity;
    }
    bool operator!=(const OrderView& other) const noexcept
    {
        return !(*this == other);
    }
};

} // namespace Proxy::Model

template <>
struct Core::Serialization::TypeTraits<Proxy::Model::Order> {
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("customer", &Proxy::Model::Order::customer),
            Core::Serialization::field("item", &Proxy::Model::Order::item),
            Core::Serialization::field("quantity", &Proxy::Model::Order::quantity));
    }
};

template <>
struct Core::Serialization::TypeTraits<Proxy::Model::OrderView> {
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("customer", &Proxy::Model::OrderView::customer),
            Core::Serialization::field("item", &Proxy::Model::OrderView::item),
            Core::Serialization::field("quantity", &Proxy::Model::OrderView::quantity));
    }
};
