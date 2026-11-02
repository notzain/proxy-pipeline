#pragma once

#include "Order.hpp"
#include <array>
#include <cstdint>

namespace Proxy::Handler::Validation {

namespace Detail {

    constexpr std::array<bool, 256> makeCustomerCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        return table;
    }

    constexpr std::array<bool, 256> makeItemCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = '0'; c <= '9'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        table['_'] = true;
        return table;
    }

    inline constexpr std::array<bool, 256> CustomerCharTable = makeCustomerCharTable();
    inline constexpr std::array<bool, 256> ItemCharTable = makeItemCharTable();

} // namespace Detail

/**
 * @brief Domain business rule validator for Order and OrderView structures.
 */
class OrderValidator {
public:
    /**
     * @brief Validates an Order instance against domain criteria.
     */
    [[nodiscard]] static bool validate(const Proxy::Model::Order& order);

    /**
     * @brief Validates an OrderView instance against domain criteria without string allocations.
     */
    [[nodiscard]] static bool validateView(const Proxy::Model::OrderView& order);

    /**
     * @brief Fast lookup-table domain validation using compile-time bitmap tables.
     */
    [[nodiscard]] static bool validateFast(const Proxy::Model::OrderView& order);
};

} // namespace Proxy::Handler::Validation
