// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/io/tap.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
struct OwnedAction
{
    std::unique_ptr<int> value;
    auto operator()(this auto && self)
    {
        return std::move(self.value);
    }
};
}  // namespace

TEST_CASE("IO tap discards complete values and executes lazily in order")
{
    using fnfelt::literals::operator""_ss;
    namespace io = fnfelt::monad::io;
    std::vector<int> order;
    auto source = io::create(
        [&]
        {
            order.push_back(1);
            return std::tuple{2, 3};
        });
    auto next = io::create(
        [&]
        {
            order.push_back(2);
            return 7;
        });
    auto action = source.tap(next);
    CHECK(order.empty());
    CHECK_EQ(action.build().sync_wait(), 7);
    CHECK_EQ(order, (std::vector{1, 2}));
    using traits_type = io::detail::TapIOTraits<>;
    static_assert(!traits_type::validate_tap(^^int, ^^int).empty());
    constexpr auto named = io::tap<"named"_ss>(io::just(std::tuple{}), io::just(5));
    static_assert(named.build().sync_wait() == 5);
    static_assert(decltype(named)::traits::name == "named");
}

TEST_CASE("IO tap consumes move-only computations")
{
    namespace io = fnfelt::monad::io;
    auto action = io::create(OwnedAction{std::make_unique<int>(1)})
                      .tap(io::create(OwnedAction{std::make_unique<int>(8)}));
    CHECK_EQ(*std::move(action).build().sync_wait(), 8);
}

TEST_CASE("IO tap preserves ordering across asynchronous stages")
{
    namespace io = fnfelt::monad::io;
    std::vector<int> order;
    auto source = io::create_async(
        [](auto, std::vector<int> * values) -> lf::task<std::tuple<int, int>>
        {
            values->push_back(1);
            co_return std::tuple{2, 3};
        },
        &order);
    auto next = io::create_async(
        [](auto, std::vector<int> * values) -> lf::task<int>
        {
            values->push_back(2);
            co_return 8;
        },
        &order);
    auto action = io::tap(std::move(source), std::move(next));
    CHECK(order.empty());
    lf::lazy_pool pool{2};
    CHECK_EQ(std::move(action).build().sync_wait(pool), 8);
    CHECK_EQ(order, (std::vector{1, 2}));
}
#endif  // DOCTEST_CONFIG_DISABLE
