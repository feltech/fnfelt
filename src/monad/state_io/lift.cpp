// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/lift.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>

TEST_CASE("state_io::lift preserves state for IO and ReaderIO sources")
{
    auto lifted = fnfelt::monad::state_io::lift<int>(fnfelt::monad::io::just(4));
    CHECK_EQ(lifted.build(8).sync_wait(), (std::pair{4, 8}));
    auto reader = fnfelt::monad::reader_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(state * 2); });
    auto lifted_reader = fnfelt::monad::state_io::lift<int>(reader);
    CHECK_EQ(lifted_reader.build(8).sync_wait(), (std::pair{16, 8}));
    auto move_state =
        fnfelt::monad::state_io::lift<std::unique_ptr<int>>(fnfelt::monad::io::just(4));
    auto result = std::move(move_state).build(std::make_unique<int>(8)).sync_wait();
    CHECK_EQ(result.first, 4);
    CHECK_EQ(*result.second, 8);
    using traits_type = fnfelt::monad::state_io::detail::LiftStateIOTraits<>;
    static_assert(!traits_type::validate_lift(^^int, ^^int).empty());
    static_assert(!traits_type::validate_lift(^^decltype(reader), ^^bool).empty());
}

TEST_CASE("lifting a ReaderIO retains borrowed state through execution")
{
    auto reader = fnfelt::monad::reader_io::create<std::string>(
        [](std::string const & state)
        { return fnfelt::monad::io::create([&state] { return state.size(); }); });
    auto lifted = fnfelt::monad::state_io::lift<std::string>(reader);
    auto result = lifted.build(std::string(1000, 'x')).sync_wait();
    CHECK_EQ(result.first, 1000);
    CHECK_EQ(result.second.size(), 1000);
}
#endif  // DOCTEST_CONFIG_DISABLE
