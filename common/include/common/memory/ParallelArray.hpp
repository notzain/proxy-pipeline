#pragma once

#include "common/compat/Span.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <tuple>
#include <utility>

namespace Common::Memory {

/**
 * @brief Fixed-capacity Structure-of-Arrays (SoA) container maintaining parallel arrays.
 *
 * Stores elements across parallel std::array<T, N> columns, keeping a single synchronized
 * active count and exposing contiguous span views over each column.
 *
 * @tparam N Maximum capacity of the parallel arrays.
 * @tparam Ts Types of each column.
 */
template <std::size_t N, typename... Ts>
class ParallelArray {
public:
    static constexpr std::size_t Capacity = N;

    constexpr ParallelArray() = default;

    /**
     * @brief Appends an element across all parallel columns in lockstep.
     */
    template <typename... Args>
    void push_back(Args&&... values)
    {
        static_assert(sizeof...(Args) == sizeof...(Ts), "Argument count must match column count");
        assert(m_size < N && "ParallelArray capacity exceeded");
        pushAt<0>(m_size, std::forward<Args>(values)...);
        ++m_size;
    }

    /**
     * @brief Accesses a mutable span over active elements of column I.
     */
    template <std::size_t I>
    [[nodiscard]] auto span() noexcept
    {
        static_assert(I < sizeof...(Ts), "Column index out of range");
        using ColType = std::tuple_element_t<I, std::tuple<Ts...>>;
        return Common::Compat::span<ColType>(std::get<I>(m_columns).data(), m_size);
    }

    /**
     * @brief Accesses a const span over active elements of column I.
     */
    template <std::size_t I>
    [[nodiscard]] auto span() const noexcept
    {
        static_assert(I < sizeof...(Ts), "Column index out of range");
        using ColType = std::tuple_element_t<I, std::tuple<Ts...>>;
        return Common::Compat::span<const ColType>(std::get<I>(m_columns).data(), m_size);
    }

    /**
     * @brief Accesses a mutable span over active elements of a column by type (for unique column types).
     */
    template <typename T>
    [[nodiscard]] auto span() noexcept
    {
        return Common::Compat::span<T>(std::get<std::array<T, N>>(m_columns).data(), m_size);
    }

    /**
     * @brief Accesses a const span over active elements of a column by type (for unique column types).
     */
    template <typename T>
    [[nodiscard]] auto span() const noexcept
    {
        return Common::Compat::span<const T>(std::get<std::array<T, N>>(m_columns).data(), m_size);
    }

    /**
     * @brief Returns a reference to the element at row index in column I.
     */
    template <std::size_t I>
    [[nodiscard]] auto& get(std::size_t row) noexcept
    {
        static_assert(I < sizeof...(Ts), "Column index out of range");
        assert(row < m_size && "Row index out of bounds");
        return std::get<I>(m_columns)[row];
    }

    /**
     * @brief Returns a const reference to the element at row index in column I.
     */
    template <std::size_t I>
    [[nodiscard]] const auto& get(std::size_t row) const noexcept
    {
        static_assert(I < sizeof...(Ts), "Column index out of range");
        assert(row < m_size && "Row index out of bounds");
        return std::get<I>(m_columns)[row];
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    /**
     * @brief Resets the active size to 0 without destroying underlying elements.
     */
    void clear() noexcept { m_size = 0; }

private:
    template <std::size_t I, typename Head, typename... Tail>
    void pushAt(std::size_t idx, Head&& head, Tail&&... tail)
    {
        std::get<I>(m_columns)[idx] = std::forward<Head>(head);
        if constexpr (sizeof...(Tail) > 0) {
            pushAt<I + 1>(idx, std::forward<Tail>(tail)...);
        }
    }

    std::tuple<std::array<Ts, N>...> m_columns { };
    std::size_t m_size { 0 };
};

} // namespace Common::Memory
