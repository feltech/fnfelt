// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/functors.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <concepts>
#include <list>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
/// Type exposing a value_of() accessor, for mem_fn/views tests.
struct Boxed
{
    /// Wrapped value read by value_of().
    int value;

    /// Reads the wrapped value.
    [[nodiscard]] constexpr int value_of() const
    {
        return value;
    }
};

/// A sample wrapped value.
inline constexpr int k_sample = 7;

/// A sample stored second value.
inline constexpr int k_second = 2;
}  // namespace

TEST_CASE("construct builds values")
{
    constexpr int made = fnfelt::construct<int>(k_sample);
    static_assert(made == k_sample);
    CHECK(made == k_sample);

    std::vector<int> const expected{1, 2, 3, 4};
    auto const filled = fnfelt::construct<std::vector<int>>(expected.begin(), expected.end());
    CHECK(filled == expected);

    auto const made_pair =
        fnfelt::construct<std::pair<int, std::string>>(k_sample, std::string{"x"});
    CHECK(made_pair.first == k_sample);
}

TEST_CASE("transform_concat moves second range onto first")
{
    std::vector<int> const first{1, 2};
    std::vector<int> const second{3, 4};
    auto const joined = fnfelt::transform_concat(first, second);
    CHECK((joined == std::vector<int>{1, 2, 3, 4}));

    // Ranges that do not support insertion are rejected by the requires-clause, without error.
    static_assert(!std::invocable<fnfelt::TransformConcat, int, int>);
    CHECK(true);
}

TEST_CASE("transform_range_to_check_non_empty tests emptiness")
{
    std::vector<int> const empty;
    std::vector<int> const values{1, 2, 3, 4};
    CHECK_FALSE(fnfelt::transform_range_to_check_non_empty(empty));
    CHECK(fnfelt::transform_range_to_check_non_empty(values));
}

TEST_CASE("transform_range_to_front_elem takes first element")
{
    std::vector<int> const values{1, 2, 3, 4};
    CHECK_EQ(fnfelt::transform_range_to_front_elem(values), 1);
}

TEST_CASE("transform_maybes_to_values drops empties and rebinds container")
{
    std::vector<std::optional<int>> const maybes{1, std::nullopt, 3, std::nullopt, 4};
    auto const values = fnfelt::transform_maybes_to_values(maybes);
    // Container type is preserved via rebind_t.
    static_assert(std::is_same_v<decltype(values), std::vector<int> const>);
    CHECK(values == (std::vector<int>{1, 3, 4}));

    std::list<std::optional<int>> const maybe_list{1, std::nullopt, 3};
    auto const value_list = fnfelt::transform_maybes_to_values(maybe_list);
    static_assert(std::is_same_v<decltype(value_list), std::list<int> const>);
    CHECK(value_list == (std::list<int>{1, 3}));

    // Ranges of non-optional-like values are rejected by the concept, without hard error.
    static_assert(!std::invocable<fnfelt::TransformMaybesToValues, std::vector<int>>);
}

TEST_CASE("TransformBoolToOptional wraps conditionally")
{
    using fnfelt::TransformBoolToOptional;
    auto const engaged = TransformBoolToOptional{.value = k_sample}(true);
    auto const empty = TransformBoolToOptional{.value = k_sample}(false);
    CHECK(engaged == std::optional<int>{k_sample});
    CHECK(empty == std::nullopt);
}

TEST_CASE("PairWith pairs first with stored second")
{
    auto const pair_with_second = fnfelt::PairWith{.second = k_second};
    auto const paired = pair_with_second(std::string{"one"});
    static_assert(std::is_same_v<decltype(paired) const, std::pair<std::string, int> const>);
    CHECK_EQ(paired.first, "one");
    CHECK_EQ(paired.second, k_second);
}

TEST_CASE("mem_fn::value_of reads accessors")
{
    CHECK_EQ(fnfelt::mem_fn::value_of(Boxed{k_sample}), k_sample);
}

TEST_CASE("attr functors read members and tuple elements")
{
    std::pair<int, int> const pair{1, 2};
    std::tuple<int, int, double> const tuple{3, 4, 5.0};
    CHECK_EQ(fnfelt::attr::first(pair), 1);
    CHECK_EQ(fnfelt::attr::second(pair), 2);
    CHECK_EQ(fnfelt::attr::get_nth<1>(tuple), 4);
}

TEST_CASE("views::value_of adapts ranges in both call styles")
{
    std::vector<Boxed> const boxes{Boxed{1}, Boxed{2}, Boxed{3}};
    std::vector<int> const expected{1, 2, 3};
    auto const piped = boxes | fnfelt::views::value_of();
    auto const called = fnfelt::views::value_of(boxes);
    CHECK(std::ranges::equal(piped, expected));
    CHECK(std::ranges::equal(called, expected));

    // Closure contract: view adaptors are semiregular.
    static_assert(std::semiregular<fnfelt::views::ValueOf>);
}

TEST_CASE("views::cast adapts ranges in both call styles")
{
    std::vector<int> const values{1, 2, 3, 4};
    std::vector<double> const expected{1.0, 2.0, 3.0, 4.0};
    auto const piped = values | fnfelt::views::cast<double>;
    auto const called = fnfelt::views::cast<double>(values);
    CHECK(std::ranges::equal(piped, expected));
    CHECK(std::ranges::equal(called, expected));

    // Closure contract: view adaptors are semiregular.
    static_assert(std::semiregular<fnfelt::views::Cast<int>>);
}
#endif  // DOCTEST_CONFIG_DISABLE
