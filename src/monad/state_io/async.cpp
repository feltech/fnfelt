// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/async.hpp>

#include <doctest/doctest.h>

#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/state_io/and_then.hpp>
#include <fnfelt/monad/state_io/just.hpp>

TEST_CASE("StateIO asynchronous leaf receives state then stored arguments")
{
    auto action = fnfelt::monad::state_io::create_async<int>(
        [](auto, int state, int value) -> lf::task<std::pair<int, int>>
        { co_return std::pair{value, state + value}; },
        2);
    CHECK_EQ(action.build(4).sync_wait(lf::lazy_pool{2}), (std::pair{2, 6}));
    auto composed =
        action.and_then([](int value) { return fnfelt::monad::state_io::just<int>(value * 2); });
    CHECK_EQ(composed.build(4).sync_wait(lf::lazy_pool{2}), (std::pair{4, 6}));
}
#endif  // DOCTEST_CONFIG_DISABLE
