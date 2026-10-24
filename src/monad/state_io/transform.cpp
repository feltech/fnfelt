// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/transform.hpp>

#include <doctest/doctest.h>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io/just.hpp>

namespace
{
struct Incomplete;
struct Immobile
{
    Immobile() = default;
    Immobile(Immobile &&) = delete;
};
struct ReturnIncomplete
{
    Incomplete operator()(std::tuple<int, int>) const;
};
struct ReturnImmobile
{
    Immobile operator()(std::tuple<int, int>) const;
};
}  // namespace

TEST_CASE("StateIO transform maps the logical value and preserves updated state")
{
    constexpr auto source = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{std::tuple{2, 3}, state + 1}); });
    constexpr auto action = source.transform([](int first, int second) { return first + second; });
    static_assert(action.build(4).sync_wait() == std::pair{5, 5});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{5, 5}));
    using traits_type = fnfelt::monad::state_io::detail::TransformStateIOTraits<>;
    static_assert(!traits_type::validate_transform(^^int, ^^int).empty());
    static_assert(!traits_type::validate_transform(^^decltype(source), ^^int).empty());
    static_assert(!traits_type::validate_transform(^^decltype(source), ^^ReturnIncomplete).empty());
    static_assert(!traits_type::validate_transform(^^decltype(source), ^^ReturnImmobile).empty());
}
#endif  // DOCTEST_CONFIG_DISABLE
