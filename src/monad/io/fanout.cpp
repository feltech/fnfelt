// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/io/fanout.hpp>

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
    auto operator()(std::tuple<int, int> &) const
    {
        return fnfelt::monad::io::just(9);
    }
    auto operator()(int &, int &) const
    {
        return fnfelt::monad::io::just(0);
    }
};
struct ConsumeInput
{
    auto operator()(std::unique_ptr<int>) const
    {
        return fnfelt::monad::io::just(1);
    }
};
}  // namespace

TEST_CASE("IO fanout eagerly maps stable shared input and lazily executes each branch")
{
    using fnfelt::literals::operator""_ss;
    namespace io = fnfelt::monad::io;
    int constructed = 0;
    std::array<int, 2> calls{};
    auto branch = [&](int marker)
    {
        return [&, marker](int & first, int & second)
        {
            ++constructed;
            return io::create(
                [&, marker, value = first + second]
                {
                    ++calls[marker - 1];
                    return value;
                });
        };
    };
    auto adapter = io::fanout(branch(1), branch(2));
    std::tuple input{2, 3};
    auto action = adapter(input);
    CHECK_EQ(constructed, 2);
    CHECK_EQ(calls, (std::array{0, 0}));
    CHECK_EQ(action.build().sync_wait(), (std::tuple{5, 5}));
    CHECK_EQ(calls, (std::array{1, 1}));
    auto direct = io::fanout<"named"_ss>(DirectBranch{})(input);
    CHECK_EQ(direct.build().sync_wait(), (std::tuple{9}));
    static_assert(decltype(direct)::traits::name == "named");
    using traits_type = io::detail::FanoutIOTraits<>;
    static_assert(!traits_type::validate_fanout_factory(^^std::tuple<int>).empty());
    static_assert(
        !traits_type::validate_fanout(^^std::tuple<ConsumeInput>, ^^std::unique_ptr<int> &)
             .empty());
    static_assert(traits_type::validate_fanout_factory(^^std::tuple<>).empty());
}

TEST_CASE("IO fanout shares move-only inputs and supports empty and move-only adapters")
{
    namespace io = fnfelt::monad::io;
    auto branch = [](std::unique_ptr<int> const & value) { return io::just(*value); };
    auto action = io::fanout(branch, branch)(std::make_unique<int>(4));
    CHECK_EQ(action.build().sync_wait(), (std::tuple{4, 4}));
    auto empty = io::fanout()(std::make_unique<int>(4));
    CHECK_EQ(empty.build().sync_wait(), std::tuple{});
    auto adapter = io::fanout([owned = std::make_unique<int>(3)](int & value)
                              { return io::just(*owned + value); });
    auto moved = std::move(adapter)(2);
    CHECK_EQ(std::move(moved).build().sync_wait(), (std::tuple{5}));
}

TEST_CASE("IO fanout sequences mixed asynchronous branches")
{
    namespace io = fnfelt::monad::io;
    auto sync = [](int & value) { return io::just(value); };
    auto async = [](int & value)
    {
        return io::create_async(
            [](auto, int input) -> lf::task<int> { co_return input + 1; }, value);
    };
    auto action = io::fanout(sync, async)(4);
    lf::lazy_pool pool{2};
    CHECK_EQ(action.build().sync_wait(pool), (std::tuple{4, 5}));
}
#endif  // DOCTEST_CONFIG_DISABLE
