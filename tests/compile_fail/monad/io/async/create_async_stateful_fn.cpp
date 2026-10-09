// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io.hpp>

#include <meta>

// A capturing lambda is stateful. libfork copies the async function object and relies on it being
// stateless, so create_async must reject it with a readable static_assert.

void construct()
{
    int state = 1;
    auto fn = [state](auto, int x) -> lf::task<int> { co_return x + state; };
    auto async_io = fnfelt::monad::io::create_async(fn, 1);
    (void)async_io;
}
