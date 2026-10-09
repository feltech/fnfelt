// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/create.hpp>

#include <meta>

// An async proxy that derives AsyncProxyTag and exposes a value_type alias, so it is detected as
// asynchronous, but lacks the stored `arg` member that execution spreads into the coroutine entry.
// Creating an IO from its action must fail with the friendly executable-proxy diagnostic rather
// than accepting it and failing later inside libfork.

namespace
{
struct DetectionOnlyProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = int;
};

struct DetectionOnlyAction
{
    constexpr DetectionOnlyProxy operator()() const
    {
        return {};
    }
};
}  // namespace

void construct()
{
    auto const io = fnfelt::monad::io::create(DetectionOnlyAction{});
    (void)io;
}
