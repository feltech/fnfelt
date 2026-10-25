// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/reader_io/split.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <tuple>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
struct DirectBranch
{
    auto operator()(std::pair<int, int>) const
    {
        return fnfelt::monad::reader_io::just<int>(9);
    }
    auto operator()(int, int) const
    {
        return fnfelt::monad::reader_io::just<int>(0);
    }
};
struct IntegerBranch
{
    auto operator()(int) const
    {
        return fnfelt::monad::reader_io::just<int>(1);
    }
};
struct WrongStateBranch
{
    auto operator()(int) const
    {
        return fnfelt::monad::reader_io::just<double>(1);
    }
};
}  // namespace

TEST_CASE("ReaderIO split pairs inputs and gives lazy branches independent environments")
{
    using fnfelt::literals::operator""_ss;
    namespace reader_io = fnfelt::monad::reader_io;
    int constructed = 0;
    std::array<int, 2> calls{};
    auto branch = [&](int value)
    {
        ++constructed;
        return reader_io::create<int>(
            [&, value](int state)
            {
                ++state;
                return fnfelt::monad::io::create(
                    [&, state, value]
                    {
                        ++calls[value - 2];
                        return state + value;
                    });
            });
    };
    auto adapter = reader_io::split<int>(branch, branch);
    auto action = adapter(std::tuple{2, 3});
    CHECK_EQ(constructed, 2);
    CHECK_EQ(calls, (std::array{0, 0}));
    CHECK_EQ(action.build(4).sync_wait(), (std::tuple{7, 8}));
    CHECK_EQ(calls, (std::array{1, 1}));
    auto direct =
        reader_io::split<"named"_ss, int>(DirectBranch{})(std::tuple<std::pair<int, int>>{{2, 3}});
    CHECK_EQ(direct.build(4).sync_wait(), (std::tuple{9}));
    static_assert(decltype(direct)::traits::name == "named");
    auto spread = reader_io::split<int>(
        [](int first, int second)
        { return reader_io::just<int>(first + second); })(std::tuple<std::pair<int, int>>{{2, 3}});
    CHECK_EQ(spread.build(4).sync_wait(), (std::tuple{5}));
    using traits_type = reader_io::detail::SplitReaderIOTraits<>;
    static_assert(!traits_type::validate_split_factory(^^std::tuple<int>, ^^int).empty());
    static_assert(!traits_type::validate_split_factory(^^std::tuple<>, ^^int &).empty());
    static_assert(!traits_type::validate_split(^^std::tuple<IntegerBranch>, ^^int, ^^int).empty());
    static_assert(
        !traits_type::validate_split(^^std::tuple<IntegerBranch>, ^^std::tuple<>, ^^int).empty());
    static_assert(
        !traits_type::validate_split(^^std::tuple<WrongStateBranch>, ^^std::tuple<int>, ^^int)
             .empty());
    static_assert(!traits_type::validate_split(
                       ^^std::tuple<IntegerBranch>, ^^std::tuple<int>, ^^std::unique_ptr<int>)
                       .empty());
}

TEST_CASE(
    "ReaderIO split consumes move-only inputs and continuations and supports empty environments")
{
    namespace reader_io = fnfelt::monad::reader_io;
    auto adapter =
        reader_io::split<int>([owned = std::make_unique<int>(3)](std::unique_ptr<int> value)
                              { return reader_io::just<int>(*owned + *value); });
    auto action = std::move(adapter)(std::tuple{std::make_unique<int>(4)});
    CHECK_EQ(std::move(action).build(2).sync_wait(), (std::tuple{7}));
    auto empty = reader_io::split<std::unique_ptr<int>>()(std::tuple{});
    CHECK_EQ(empty.build(std::make_unique<int>(2)).sync_wait(), std::tuple{});
}

TEST_CASE("ReaderIO split sequences mixed asynchronous branches")
{
    namespace reader_io = fnfelt::monad::reader_io;
    auto sync = [](int value) { return reader_io::just<int>(value); };
    auto async = [](int value)
    {
        return reader_io::create<int>(
            [value](int state)
            {
                return fnfelt::monad::io::create_async(
                    [](auto, int input) -> lf::task<int> { co_return input; }, value + state);
            });
    };
    auto action = reader_io::split<int>(sync, async)(std::tuple{4, 6});
    lf::lazy_pool pool{2};
    CHECK_EQ(action.build(2).sync_wait(pool), (std::tuple{4, 8}));
}
#endif  // DOCTEST_CONFIG_DISABLE
