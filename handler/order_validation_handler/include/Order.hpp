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

    bool operator==(const Order& other) const = default;
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

