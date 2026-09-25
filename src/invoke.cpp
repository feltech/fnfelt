// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/invoke.hpp>

#include <doctest/doctest.h>

#include <array>
#include <concepts>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
/// Functor invocable with any number of int arguments, to pin the tuple-spreading branch.
struct SumInts
{
    /// Sums all arguments.
    template <class... TArgs>
    // NOLINTNEXTLINE(*-unused-template)
    requires(std::convertible_to<TArgs, int> &&...) constexpr int operator()(TArgs... args) const
    {
        return (static_cast<int>(args) + ...);
    }
};

/// Functor invocable with any arguments, e.g. a vector's element AND allocator types.
struct CountAny
{
    /// Counts the arguments.
    template <class... TArgs>
    // NOLINTNEXTLINE(*-unused-template)
    constexpr int operator()(TArgs &&... /*args*/) const
    {
        return static_cast<int>(sizeof...(TArgs));
    }
};

/**
 * Functor invocable BOTH with (int, int) and with a single std::tuple<int, int> argument.
 *
 * Used to pin that the direct (non-spreading) branch of invoke_or_apply_result_t wins.
 */
struct Both
{
    /// Spreading form: adds the tuple elements.
    constexpr int operator()(int lhs, int rhs) const
    {
        return lhs + rhs;
    }
    /// Direct form: yields a distinct marker type.
    constexpr char operator()(std::tuple<int, int> /*tuple*/) const
    {
        return 'x';
    }
};

/// Identity functor accepting any single argument directly.
struct Identity
{
    /// Forwards value.
    constexpr int operator()(int value) const
    {
        return value;
    }
};
}  // namespace

TEST_CASE("applicable_with spreads tuple-like values")
{
    static_assert(fnfelt::applicable_with<SumInts, std::tuple<int, int>>);
    static_assert(fnfelt::applicable_with<SumInts, std::pair<int, int>>);
    // std::vector<int> spreads ALL type arguments, i.e. (int, std::allocator<int>).
    static_assert(fnfelt::applicable_with<CountAny, std::vector<int>>);
    static_assert(fnfelt::applicable_with<SumInts const &, std::tuple<int, int> const &>);
}

TEST_CASE("applicable_with rejects mismatched and non-tuple-like values")
{
    // Callable does not accept the spread elements.
    static_assert(!fnfelt::applicable_with<SumInts, std::tuple<int, std::string>>);
    static_assert(!fnfelt::applicable_with<SumInts, std::pair<int, std::string>>);

    // Not a type-parameter-only specialization (non-type parameter).
    static_assert(!fnfelt::applicable_with<SumInts, std::array<int, 2>>);

    // Not a specialization at all.
    static_assert(!fnfelt::applicable_with<SumInts, int>);

    // Direct invocability alone does not satisfy the spreading concept.
    static_assert(!fnfelt::applicable_with<Identity, int>);
}

TEST_CASE("applicable_with decays callable and value")
{
    static_assert(fnfelt::applicable_with<SumInts &, std::tuple<int, int> const>);
    static_assert(!fnfelt::applicable_with<SumInts &, std::tuple<std::string, int> const>);
}

TEST_CASE("invoke_or_apply_result_t direct branch")
{
    static_assert(std::is_same_v<fnfelt::invoke_or_apply_result_t<Identity, int>, int>);
    // cv/ref-qualified values are decayed before dispatch, so the direct branch still applies.
    static_assert(std::is_same_v<fnfelt::invoke_or_apply_result_t<Identity, int const &>, int>);
}

TEST_CASE("invoke_or_apply_result_t tuple branch")
{
    static_assert(
        std::is_same_v<fnfelt::invoke_or_apply_result_t<SumInts, std::tuple<int, int>>, int>);
    static_assert(
        std::is_same_v<fnfelt::invoke_or_apply_result_t<SumInts, std::pair<int, int>>, int>);
    // The decay fix: cv/ref-qualified tuple-like values also dispatch (previously a hard error).
    static_assert(std::is_same_v<
                  fnfelt::invoke_or_apply_result_t<SumInts, std::tuple<int, int> const &>,
                  int>);
}

TEST_CASE("invoke_or_apply_result_t direct branch wins over tuple spreading")
{
    // Both is invocable with (int, int) AND directly with std::tuple<int, int> - the direct
    // branch must win, yielding char rather than int.
    static_assert(
        std::is_same_v<fnfelt::invoke_or_apply_result_t<Both, std::tuple<int, int>>, char>);
}

TEST_CASE("direct branch result is used at runtime")
{
    constexpr Both both{};
    constexpr char direct_branch_result = both(std::tuple{7, 7});
    static_assert(direct_branch_result == 'x');
    CHECK_EQ(both(std::tuple{7, 7}), 'x');
}
#endif  // DOCTEST_CONFIG_DISABLE
