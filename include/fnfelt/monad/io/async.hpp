// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file async.hpp
 *
 * Run proxy for the IO monad.
 *
 * `IO::operator()` yields a `RunProxy` holding the IO's action; calling `sync_wait` on it is the
 * only execution path.
 */
#pragma once

#include <meta>

#include <utility>

#include <fnfelt/monad/io/detail.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{

/**
 * Proxy that runs an IO's action.
 *
 * `sync_wait` dispatches on whether the action is asynchronous (its invocation result derives from
 * @c AsyncProxyTag): synchronous actions are run inline and are constant-evaluable, while
 * asynchronous actions are not yet supported and are rejected with a readable `static_assert`.
 *
 * @tparam TAction Callable action held by value.
 */
template <class TAction>
struct RunProxy
{
    /// The action to run.
    TAction action;

    /**
     * Run the held action and return its value.
     *
     * Scheduler arguments are accepted uniformly at every call site and ignored on the synchronous
     * path. Asynchronous execution is not implemented yet.
     *
     * @param self The proxy to run (explicit object parameter).
     * @return The value the action produces.
     */
    [[nodiscard]] constexpr auto sync_wait(this auto && self, auto &&... /*scheduler*/)
    {
        if constexpr (detail::action_is_async(^^TAction))
        {
            static_assert(false, "async IO actions cannot be run yet");
            return;
        }
        if constexpr (!detail::action_is_async(^^TAction))
        {
            return FW(self).action();
        }
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
