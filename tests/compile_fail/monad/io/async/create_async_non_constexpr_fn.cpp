// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io.hpp>

#include <meta>

// A stateless coroutine functor with a non-constexpr default constructor cannot be stored as the
// proxy's `static constexpr TFn`, so create_async must reject it with a readable static_assert
// rather than hard-erroring inside the proxy.

namespace
{
struct NonConstexprCtorFn
{
    NonConstexprCtorFn();
    auto operator()(auto, int value) const -> lf::task<int>
    {
        co_return value;
    }
};
}  // namespace

void construct()
{
    auto async_io = fnfelt::monad::io::create_async(NonConstexprCtorFn{}, 1);
    (void)async_io;
}
