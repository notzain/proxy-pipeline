#pragma once

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace Common::Types {

/**
 * @brief Helper template for combining multiple callables into an overloaded callable object.
 *
 * Typically used with `Variant::match` to pattern-match variant alternatives using lambda overloads.
 *
 * @tparam Fs Functor or lambda types to combine.
 */
template <typename... Fs>
struct Overload : Fs... {
    using Fs::operator()...;
};

/**
 * @brief Deduction guide for Overload.
 */
template <typename... Fs>
Overload(Fs...) -> Overload<Fs...>;

/**
 * @brief Ergonomic wrapper around std::variant.
 *
 * Inherits from `std::variant<Ts...>` to provide seamless interop with standard variant algorithms
 * and type traits, while augmenting it with intuitive member functions:
 * - `is<T>()`: replaces `std::holds_alternative<T>`
 * - `get<T>()`: replaces `std::get<T>` and `std::get_if<T>`, returning `std::optional`
 * - `match(overloads...)`: replaces `std::visit`, constructing an overload set on the fly
 *
 * @tparam Ts Allowed alternative types stored in the variant.
 */
template <typename... Ts>
class Variant : public std::variant<Ts...> {
public:
    using Base = std::variant<Ts...>;
    using Base::Base;
    using Base::operator=;

    Variant() = default;
    Variant(const Variant&) = default;
    Variant(Variant&&) noexcept = default;
    Variant& operator=(const Variant&) = default;
    Variant& operator=(Variant&&) noexcept = default;
    ~Variant() = default;

    /**
     * @brief Implicit converting constructor from underlying std::variant.
     */
    Variant(const Base& other)
        : Base(other)
    {
    }

    /**
     * @brief Implicit converting move constructor from underlying std::variant.
     */
    Variant(Base&& other) noexcept
        : Base(std::move(other))
    {
    }

    /**
     * @brief Checks whether the variant currently holds alternative type T.
     *
     * Equivalent to `std::holds_alternative<T>(*this)`.
     *
     * @tparam T Type to check. Must be one of Ts...
     * @return true if variant currently contains type T, false otherwise.
     */
    template <typename T>
    [[nodiscard]] constexpr bool is() const noexcept
    {
        return std::holds_alternative<T>(*this);
    }

    /**
     * @brief Retrieves an optional reference to the contained value of type T (lvalue ref).
     *
     * Replaces `std::get_if<T>` and `std::get<T>`. Returns `std::nullopt` if the variant
     * does not currently hold alternative T.
     *
     * @tparam T Desired alternative type.
     * @return std::optional<std::reference_wrapper<T>> reference if active, nullopt otherwise.
     */
    template <typename T>
    [[nodiscard]] constexpr auto get() & noexcept
    {
        using ReturnType = std::optional<std::conditional_t<std::is_const_v<T>, std::reference_wrapper<const std::remove_cv_t<T>>, std::reference_wrapper<std::remove_cv_t<T>>>>;
        auto* ptr = std::get_if<std::remove_cv_t<T>>(this);
        if (ptr) {
            return ReturnType(*ptr);
        }
        return ReturnType(std::nullopt);
    }

    /**
     * @brief Retrieves an optional const reference to the contained value of type T (const lvalue ref).
     *
     * @tparam T Desired alternative type.
     * @return std::optional<std::reference_wrapper<const T>> reference if active, nullopt otherwise.
     */
    template <typename T>
    [[nodiscard]] constexpr auto get() const& noexcept
    {
        using ReturnType = std::optional<std::reference_wrapper<const std::remove_cv_t<T>>>;
        const auto* ptr = std::get_if<std::remove_cv_t<T>>(this);
        if (ptr) {
            return ReturnType(*ptr);
        }
        return ReturnType(std::nullopt);
    }

    /**
     * @brief Retrieves an optional value of type T by moving out of an rvalue Variant.
     *
     * @tparam T Desired alternative type.
     * @return std::optional<T> containing the moved value if active, nullopt otherwise.
     */
    template <typename T>
    [[nodiscard]] constexpr std::optional<std::remove_cv_t<T>> get() &&
    {
        auto* ptr = std::get_if<std::remove_cv_t<T>>(this);
        if (ptr) {
            return std::optional<std::remove_cv_t<T>>(std::move(*ptr));
        }
        return std::nullopt;
    }

    /**
     * @brief Pattern-matches against the contained value using an overload set of callables.
     *
     * Uses the overload pattern to combine all passed callables and invokes `std::visit`.
     *
     * @tparam Fs Callable types (lambdas, function objects).
     * @param callables One or more callables covering the variant alternatives.
     * @return The common return type of the matched visitor callable.
     */
    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) &
    {
        return std::visit(Overload { std::forward<Fs>(callables)... }, static_cast<Base&>(*this));
    }

    /**
     * @brief Pattern-matches against const variant using an overload set of callables.
     */
    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) const&
    {
        return std::visit(Overload { std::forward<Fs>(callables)... }, static_cast<const Base&>(*this));
    }

    /**
     * @brief Pattern-matches against rvalue variant using an overload set of callables.
     */
    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) &&
    {
        return std::visit(Overload { std::forward<Fs>(callables)... }, static_cast<Base&&>(*this));
    }
};

} // namespace Common::Types
