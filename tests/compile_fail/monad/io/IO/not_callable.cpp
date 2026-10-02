// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

// CallableWithArg requires an argument, so it is not callable with no arguments and IO must fail
// with the friendly static_assert message naming the failing precondition.

namespace
{
/// Callable struct requiring an int argument.
struct CallableWithArg
{
    int operator()(int) const
    {
        return 1;
    }
};
}  // namespace

void construct()
{
    fnfelt::monad::io::IO<CallableWithArg> const io_monad{CallableWithArg{}};
}