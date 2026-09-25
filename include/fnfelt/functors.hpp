// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file functors.hpp
 *
 * Small higher-order functions useful for range transformations and monadic pipelines.
 */
#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>

#include <fnfelt/macros_push.hpp>
#include <fnfelt/type_traits.hpp>

namespace fnfelt
{
/**
 * Constructs a T from its constructor arguments.
 *
 * @tparam T Type to construct.
 */
template <class T>
struct Construct
{
    /**
     * Constructs a T from TArgs.
     *
     * @tparam TArgs Constructor argument types.
     * @param args Arguments forwarded to T's brace-initialization.
     * @return The constructed T.
     */
    template <class... TArgs>
    constexpr T operator()(TArgs &&... args) const
    {
        return T{FW(args)...};
    }
};

/**
 * Instance of Construct, call as construct<T>(args...).
 *
 * @tparam T Type to construct.
 */
template <class T>
inline constexpr Construct<T> construct{};

/**
 * Concatenates two ranges by moving the elements of the second onto the end of the first.
 *
 * The first range is returned by value, so it must own its elements and support insertion at its
 * end.
 */
struct TransformConcat
{
    /**
     * Appends the elements of second onto first.
     *
     * @tparam TFirst First range type.
     * @tparam TSecond Second range type.
     * @param first Range to append to, returned by value.
     * @param second Range whose elements are moved onto the end of first.
     * @return first with the elements of second appended.
     */
    template <class TFirst, class TSecond>
    requires std::ranges::input_range<TFirst> && std::ranges::input_range<TSecond> && requires(
        TFirst first, TSecond second)
    {
        first.insert(
            std::ranges::end(first),
            std::make_move_iterator(std::ranges::begin(second)),
            std::make_move_iterator(std::ranges::end(second)));
    }
    constexpr auto operator()(TFirst first, TSecond second) const
    {
        first.insert(
            std::ranges::end(first),
            std::make_move_iterator(std::ranges::begin(second)),
            std::make_move_iterator(std::ranges::end(second)));
        return first;
    }
};

/// Instance of TransformConcat.
inline constexpr TransformConcat transform_concat{};

/// Maps a range to whether or not it is empty.
struct TransformRangeToCheckNonEmpty
{
    /**
     * Tests whether values is empty.
     *
     * @tparam TRange Range type.
     * @param values Range to test.
     * @return True when values is not empty.
     */
    template <std::ranges::range TRange>
    constexpr auto operator()(TRange && values) const
    {
        return !std::ranges::empty(FW(values));
    }
};

/// Instance of TransformRangeToCheckNonEmpty.
inline constexpr TransformRangeToCheckNonEmpty transform_range_to_check_non_empty{};

/// Maps a range to its first element, by value.
struct TransformRangeToFrontElem
{
    /**
     * Returns the first element of values.
     *
     * @tparam TRange Range type.
     * @param values Range to take the first element of.
     * @return Copy of the first element.
     */
    template <std::ranges::range TRange>
    constexpr auto operator()(TRange && values) const
    {
        return FW(values).front();
    }
};

/// Instance of TransformRangeToFrontElem.
inline constexpr TransformRangeToFrontElem transform_range_to_front_elem{};

namespace detail
{
/**
 * Concept satisfied when T wraps an optional value.
 *
 * Mirrors the optional-like members of std::optional (and std::expected) loosely enough to accept
 * custom maybe types: a nested value_type, a has_value() test, and dereference.
 *
 * @tparam T Type to test.
 */
template <class T>
concept optional_like = requires(T maybe)
{
    typename T::value_type;
    {maybe.has_value()}->std::convertible_to<bool>;
    *maybe;
};
}  // namespace detail

/// Maps a range of optionals to a range of the contained values, dropping the empties.
struct TransformMaybesToValues
{
    /**
     * Extracts the engaged values from a range of optionals.
     *
     * @tparam TRange Range of optional-like types.
     * @param values Range of optionals, consumed by value.
     * @return Range of the non-empty optionals' values, rebinding TRange to the value type.
     */
    template <std::ranges::range TRange>
    requires detail::optional_like<std::ranges::range_value_t<TRange>> constexpr auto operator()(
        TRange values) const
    {
        using maybe_t = std::ranges::range_value_t<TRange>;
        return std::move(values) |
            std::views::filter([](maybe_t const & maybe) { return maybe.has_value(); }) |
            std::views::transform(
                   [](maybe_t & maybe) -> decltype(auto) { return std::move(*maybe); }) |
            std::ranges::to<rebind_t<TRange, typename maybe_t::value_type>>();
    }
};

/// Instance of TransformMaybesToValues.
inline constexpr TransformMaybesToValues transform_maybes_to_values{};

/**
 * Maps a bool to an optional, yielding the stored value when true and nullopt when false.
 *
 * @tparam TValue Type of the stored value.
 */
template <class TValue>
struct TransformBoolToOptional
{
    /// Value yielded when the condition is true.
    TValue value;

    /**
     * Conditionally wraps the stored value in an optional.
     *
     * @param self The stored value is read from this object's @c value.
     * @param cond Condition deciding whether the value is present.
     * @return The value when cond is true, otherwise std::nullopt.
     */
    constexpr std::optional<TValue> operator()(this auto && self, bool cond)
    {
        return cond ? std::optional<TValue>{FW(self).value} : std::nullopt;
    }
};

/**
 * Pairs a stored second value with a first value supplied at call time.
 *
 * @tparam TSecond Type of the stored second value.
 */
template <class TSecond>
struct PairWith
{
    /// Value used as the second element of the pair.
    TSecond second;

    /**
     * Pairs first with the stored second.
     *
     * @param self The stored value is read from this object's @c second.
     * @param first Value used as the first element of the pair.
     * @return Pair of first and the stored second.
     */
    constexpr auto operator()(this auto && self, auto && first)
    {
        return std::pair{FW(first), FW(self).second};
    }
};

namespace mem_fn
{
/// Read a member's value via its value_of() accessor.
struct ValueOf
{
    /**
     * Calls obj.value_of().
     *
     * @tparam T Object type.
     * @param obj Object to read.
     * @return Result of obj.value_of().
     */
    template <class T>
    constexpr auto operator()(T && obj) const -> decltype(auto)
    {
        return FW(obj).value_of();
    }
};

/// Instance of ValueOf.
inline constexpr ValueOf value_of{};
}  // namespace mem_fn

namespace attr
{
/// Read a member's @c first member.
struct First
{
    /**
     * Reads obj.first.
     *
     * @tparam T Object type.
     * @param obj Object to read.
     * @return obj.first.
     */
    template <class T>
    constexpr auto operator()(T && obj) const -> decltype(auto)
    {
        return FW(obj).first;
    }
};

/// Instance of First.
inline constexpr First first{};

/// Read a member's @c second member.
struct Second
{
    /**
     * Reads obj.second.
     *
     * @tparam T Object type.
     * @param obj Object to read.
     * @return obj.second.
     */
    template <class T>
    constexpr auto operator()(T && obj) const -> decltype(auto)
    {
        return FW(obj).second;
    }
};

/// Instance of Second.
inline constexpr Second second{};

/**
 * Read a tuple-like object's idx'th element.
 *
 * @tparam idx Element index to read.
 */
template <std::size_t idx>
struct GetNth
{
    /**
     * Reads std::get<idx>(obj).
     *
     * @tparam T Object type.
     * @param obj Tuple-like object to read.
     * @return idx'th element of obj.
     */
    template <class T>
    constexpr auto operator()(T && obj) const -> decltype(auto)
    {
        return std::get<idx>(FW(obj));
    }
};

/**
 * Instance of GetNth, call as get_nth<I>(tuple_like).
 *
 * @tparam idx Element index to read.
 */
template <std::size_t idx>
inline constexpr GetNth<idx> get_nth{};
}  // namespace attr

namespace views
{
/// Range adaptor that applies mem_fn::value_of to each element.
struct ValueOf : std::ranges::range_adaptor_closure<ValueOf>
{
    /**
     * Returns the value_of transform as a range adaptor closure.
     *
     * @return Range adaptor closure applying mem_fn::value_of to each element.
     */
    constexpr auto operator()() const
    {
        return std::views::transform(mem_fn::value_of);
    }

    /**
     * Applies mem_fn::value_of to each element of range.
     *
     * @tparam TRange Range type.
     * @param range Range to transform.
     * @return View of each element's value_of() result.
     */
    template <std::ranges::range TRange>
    constexpr auto operator()(TRange && range) const
    {
        return FW(range) | std::views::transform(mem_fn::value_of);
    }
};

/// Instance of ValueOf, usable as views::value_of() or views::value_of(range).
inline constexpr ValueOf value_of{};

/**
 * Range adaptor that static_casts each element to TTarget.
 *
 * @tparam TTarget Type to cast each element to.
 */
template <class TTarget>
struct Cast : std::ranges::range_adaptor_closure<Cast<TTarget>>
{
    /**
     * Casts each element of range to TTarget.
     *
     * @tparam TRange Range type.
     * @param range Range to transform.
     * @return View of each element cast to TTarget.
     */
    template <std::ranges::range TRange>
    constexpr auto operator()(TRange && range) const
    {
        return FW(range) |
            std::views::transform(
                   [](auto && obj) -> decltype(auto) { return static_cast<TTarget>(FW(obj)); });
    }
};

/**
 * Instance of Cast, call as views::cast<TTarget>(range).
 *
 * @tparam TTarget Type to cast each element to.
 */
template <class TTarget>
inline constexpr Cast<TTarget> cast{};
}  // namespace views
}  // namespace fnfelt

#include <fnfelt/macros_pop.hpp>
