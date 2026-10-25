// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/reader_io/fanout.hpp>

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
    auto operator()(std::tuple<int, int> &) const
    {
        return fnfelt::monad::reader_io::just<int>(9);
    }
    auto operator()(int &, int &) const
    {
        return fnfelt::monad::reader_io::just<int>(0);
    }
};
struct WrongStateBranch
{
    auto operator()(int &) const
    {
        return fnfelt::monad::reader_io::just<double>(1);
    }
};
}  // namespace

TEST_CASE("ReaderIO fanout eagerly maps input and lazily gives branches independent environments")
{
    using fnfelt::literals::operator""_ss;
    namespace reader_io = fnfelt::monad::reader_io;
    int constructed = 0;
    std::array<int, 2> calls{};
    auto branch = [&](int marker)
    {
        return [&, marker](int & first, int & second)
        {
            ++constructed;
            return reader_io::create<int>(
                [&, marker, value = first + second](int state)
                {
                    ++state;
                    return fnfelt::monad::io::create(
                        [&, state, marker, value]
                        {
                            ++calls[marker - 1];
                            return state + value;
                        });
                });
        };
    };
    auto adapter = reader_io::fanout<int>(branch(1), branch(2));
    std::tuple input{2, 3};
    auto action = adapter(input);
    CHECK_EQ(constructed, 2);
    CHECK_EQ(calls, (std::array{0, 0}));
    CHECK_EQ(action.build(4).sync_wait(), (std::tuple{10, 10}));
    CHECK_EQ(calls, (std::array{1, 1}));
    auto direct = reader_io::fanout<"named"_ss, int>(DirectBranch{})(input);
    CHECK_EQ(direct.build(4).sync_wait(), (std::tuple{9}));
    static_assert(decltype(direct)::traits::name == "named");
    using traits_type = reader_io::detail::FanoutReaderIOTraits<>;
    static_assert(!traits_type::validate_fanout_factory(^^std::tuple<int>, ^^int).empty());
    static_assert(!traits_type::validate_fanout_factory(^^std::tuple<>, ^^int &).empty());
    static_assert(
        !traits_type::validate_fanout(^^std::tuple<WrongStateBranch>, ^^int &, ^^int).empty());
    static_assert(!traits_type::validate_fanout(
                       ^^std::tuple<DirectBranch>, ^^std::tuple<int, int> &, ^^std::unique_ptr<int>)
                       .empty());
}

TEST_CASE("ReaderIO fanout supports move-only inputs, continuations and empty environments")
{
    namespace reader_io = fnfelt::monad::reader_io;
    auto branch = [](std::unique_ptr<int> const & value) { return reader_io::just<int>(*value); };
    auto action = reader_io::fanout<int>(branch, branch)(std::make_unique<int>(4));
    CHECK_EQ(action.build(2).sync_wait(), (std::tuple{4, 4}));
    auto adapter = reader_io::fanout<int>([owned = std::make_unique<int>(3)](int & value)
                                          { return reader_io::just<int>(*owned + value); });
    auto moved = std::move(adapter)(2);
    CHECK_EQ(std::move(moved).build(4).sync_wait(), (std::tuple{5}));
    auto empty = reader_io::fanout<std::unique_ptr<int>>()(4);
    CHECK_EQ(empty.build(std::make_unique<int>(2)).sync_wait(), std::tuple{});
}

TEST_CASE("ReaderIO fanout sequences mixed asynchronous branches")
{
    namespace reader_io = fnfelt::monad::reader_io;
    auto sync = [](int & value) { return reader_io::just<int>(value); };
    auto async = [](int & value)
    {
        return reader_io::create<int>(
            [value](int state)
            {
                return fnfelt::monad::io::create_async(
                    [](auto, int input) -> lf::task<int> { co_return input; }, value + state);
            });
    };
    auto action = reader_io::fanout<int>(sync, async)(4);
    lf::lazy_pool pool{2};
    CHECK_EQ(action.build(2).sync_wait(pool), (std::tuple{4, 6}));
}
#endif  // DOCTEST_CONFIG_DISABLE
