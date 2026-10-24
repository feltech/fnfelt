// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io/repeat_until.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/state_io/async.hpp>

TEST_CASE("StateIO repeat_until threads state independently of its accumulator")
{
    constexpr auto source = fnfelt::monad::state_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(std::pair{state, state + 1}); });
    constexpr auto action = source.repeat_until(
        0,
        [](int accumulator, int value) -> std::expected<int, int>
        {
            if (accumulator == 2)
            {
                return value;
            }
            return std::unexpected{accumulator + 1};
        });
    static_assert(action.build(4).sync_wait() == std::pair{6, 7});
    CHECK_EQ(action.build(4).sync_wait(), (std::pair{6, 7}));
}

TEST_CASE("StateIO asynchronous repeat_until supports move-only accumulators")
{
    auto source = fnfelt::monad::state_io::create_async<int>(
        [](auto, int state) -> lf::task<std::pair<int, int>>
        { co_return std::pair{state, state + 1}; });
    auto action = source.repeat_until(
        std::make_unique<int>(0),
        [](std::unique_ptr<int> accumulator, int value) -> std::expected<int, std::unique_ptr<int>>
        {
            if (++*accumulator == 3)
            {
                return value;
            }
            return std::unexpected{std::move(accumulator)};
        });
    CHECK_EQ(std::move(action).build(4).sync_wait(lf::lazy_pool{2}), (std::pair{6, 7}));
}
#endif  // DOCTEST_CONFIG_DISABLE
