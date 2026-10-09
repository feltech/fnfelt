// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

// The run proxy must be consumed as an rvalue: `sync_wait` invokes the stored action as an rvalue,
// so calling it on a stored lvalue proxy must fail with the friendly static_assert.

namespace
{
/// Valid action.
int action()
{
    return 1;
}
}  // namespace

void run()
{
    auto proxy = fnfelt::monad::io::IO<int (*)()>{&action}.build();
    (void)proxy.sync_wait();
}
