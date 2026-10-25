// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/io/split.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <tuple>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
struct DirectBranch
{
    auto operator()(std::pair<int, int>) const
    {
        return fnfelt::monad::io::just(9);
    }
    auto operator()(int, int) const
    {
        return fnfelt::monad::io::just(0);
    }
};
struct IntegerBranch
{
    auto operator()(int) const
    {
        return fnfelt::monad::io::just(1);
    }
};
}  // namespace

TEST_CASE("IO split eagerly pairs inputs and lazily executes each branch")
{
    using fnfelt::literals::operator""_ss;
    namespace io = fnfelt::monad::io;
    int constructed = 0;
    std::array<int, 2> calls{};
    auto branch = [&](int value)
    {
        ++constructed;
        return io::create(
            [&, value]
            {
                ++calls[value - 2];
                return value;
            });
    };
    auto adapter = io::split(branch, branch);
    auto action = adapter(std::tuple{2, 3});
    CHECK_EQ(constructed, 2);
    CHECK_EQ(calls, (std::array{0, 0}));
    CHECK_EQ(action.build().sync_wait(), (std::tuple{2, 3}));
    CHECK_EQ(calls, (std::array{1, 1}));
    auto direct = io::split<"named"_ss>(DirectBranch{})(std::tuple<std::pair<int, int>>{{2, 3}});
    CHECK_EQ(direct.build().sync_wait(), (std::tuple{9}));
    static_assert(decltype(direct)::traits::name == "named");
    auto spread = io::split([](int first, int second) { return io::just(first + second); })(
        std::tuple<std::pair<int, int>>{{2, 3}});
    CHECK_EQ(spread.build().sync_wait(), (std::tuple{5}));
    using traits_type = io::detail::SplitIOTraits<>;
    static_assert(!traits_type::validate_split_factory(^^std::tuple<int>).empty());
    static_assert(!traits_type::validate_split(^^std::tuple<IntegerBranch>, ^^int).empty());
    static_assert(
        !traits_type::validate_split(^^std::tuple<IntegerBranch>, ^^std::tuple<>).empty());
    static_assert(!traits_type::validate_split(
                       ^^std::tuple<IntegerBranch>, ^^std::tuple<std::unique_ptr<int>>)
                       .empty());
}

TEST_CASE("IO split consumes move-only inputs and stored continuations")
{
    namespace io = fnfelt::monad::io;
    auto adapter = io::split([owned = std::make_unique<int>(3)](std::unique_ptr<int> value)
                             { return io::just(*owned + *value); });
    auto action = std::move(adapter)(std::tuple{std::make_unique<int>(4)});
    CHECK_EQ(std::move(action).build().sync_wait(), (std::tuple{7}));
    auto empty = io::split()(std::tuple{});
    CHECK_EQ(empty.build().sync_wait(), std::tuple{});
}

TEST_CASE("IO split sequences mixed asynchronous branches")
{
    namespace io = fnfelt::monad::io;
    auto sync = [](int value) { return io::just(value); };
    auto async = [](int value)
    {
        return io::create_async(
            [](auto, int input) -> lf::task<int> { co_return input + 1; }, value);
    };
    auto action = io::split(sync, async)(std::tuple{4, 6});
    lf::lazy_pool pool{2};
    CHECK_EQ(action.build().sync_wait(pool), (std::tuple{4, 7}));
}
#endif  // DOCTEST_CONFIG_DISABLE
