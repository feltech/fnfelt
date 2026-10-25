// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/reader_io/tap.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

TEST_CASE("ReaderIO tap discards a tuple and gives both stages independent environments")
{
    using fnfelt::literals::operator""_ss;
    namespace reader_io = fnfelt::monad::reader_io;
    std::vector<int> order;
    auto source = reader_io::create<int>(
        [&](int state)
        {
            ++state;
            return fnfelt::monad::io::create(
                [&, state]()
                {
                    order.push_back(state);
                    return std::tuple{2, 3};
                });
        });
    auto next = reader_io::create<int>(
        [&](int state)
        {
            return fnfelt::monad::io::create(
                [&, state]()
                {
                    order.push_back(state);
                    return state;
                });
        });
    auto action = source.tap(next);
    CHECK(order.empty());
    CHECK_EQ(action.build(4).sync_wait(), 4);
    CHECK_EQ(order, (std::vector{5, 4}));
    constexpr auto named =
        reader_io::just<int>(std::tuple{}).tap<"named"_ss>(reader_io::just<int>(8));
    static_assert(named.build(4).sync_wait() == 8);
    static_assert(decltype(named)::traits::name == "named");
    using traits_type = reader_io::detail::TapReaderIOTraits<>;
    static_assert(!traits_type::validate_tap(^^int, ^^int).empty());
    using source_type = decltype(reader_io::just<std::unique_ptr<int>>(1));
    using wrong_state_type = decltype(reader_io::just<double>(1));
    static_assert(!traits_type::validate_tap(^^source_type, ^^source_type).empty());
    static_assert(!traits_type::validate_tap(^^decltype(source), ^^wrong_state_type).empty());
}

TEST_CASE("ReaderIO tap preserves asynchronous stage ordering")
{
    namespace reader_io = fnfelt::monad::reader_io;
    std::vector<int> order;
    auto source = reader_io::create<int>(
        [&](int state)
        {
            return fnfelt::monad::io::create_async(
                [](auto, std::vector<int> * values, int value) -> lf::task<int>
                {
                    values->push_back(value);
                    co_return value;
                },
                &order,
                state + 1);
        });
    auto next = reader_io::create<int>(
        [&](int state)
        {
            return fnfelt::monad::io::create_async(
                [](auto, std::vector<int> * values, int value) -> lf::task<int>
                {
                    values->push_back(value);
                    co_return value;
                },
                &order,
                state);
        });
    auto action = reader_io::tap(source, next);
    CHECK(order.empty());
    lf::lazy_pool pool{2};
    CHECK_EQ(action.build(4).sync_wait(pool), 4);
    CHECK_EQ(order, (std::vector{5, 4}));
}
#endif  // DOCTEST_CONFIG_DISABLE
