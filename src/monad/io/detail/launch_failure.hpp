// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file monad/io/detail/launch_failure.hpp
 *
 * Test fixtures for exceptions while launching concurrent children.
 */
#pragma once

#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fnfelt/monad/io/async.hpp>

namespace launch_failure_test
{
struct State
{
    std::atomic<bool> armed{false};
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> timed_out{false};
};

inline bool wait(std::atomic<bool> const & flag)
{
    constexpr int spin_limit = 10000000;
    for (int spin = 0; spin < spin_limit; ++spin)
    {
        if (flag.load(std::memory_order_acquire))
        {
            return true;
        }
        if (spin % 1024 == 0)
        {
            std::this_thread::yield();
        }
    }
    return false;
}

template <class TValue>
struct Action
{
    State * state;
    bool is_first;

    Action(State * state_in, bool is_first_in) : state{state_in}, is_first{is_first_in} {}
    Action(Action const &) = default;
    Action & operator=(Action const &) = default;
    Action(Action && other) : state{other.state}, is_first{other.is_first}
    {
        if (!is_first && state->armed.load(std::memory_order_acquire))
        {
            if (!wait(state->started))
            {
                state->timed_out.store(true, std::memory_order_relaxed);
            }
            state->release.store(true, std::memory_order_release);
            throw std::runtime_error("child launch failure");
        }
    }
    Action & operator=(Action &&) = default;

    auto operator()() const
    {
        constexpr auto child = [](auto, State * shared) -> lf::task<TValue>
        {
            shared->started.store(true, std::memory_order_release);
            if (!wait(shared->release))
            {
                shared->timed_out.store(true, std::memory_order_relaxed);
            }
            shared->finished.store(true, std::memory_order_release);
            co_return TValue{};
        };
        state->armed.store(true, std::memory_order_release);
        auto leaf = fnfelt::monad::io::create_async(child, state);
        return std::move(leaf).build().action();
    }
};
}  // namespace launch_failure_test
