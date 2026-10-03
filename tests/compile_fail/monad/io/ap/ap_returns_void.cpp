// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/ap.hpp>

// ap with default traits must reject a function IO whose value (a callable taking an int and
// returning void) would produce a void application result, failing with the friendly
// static_assert message rather than a template error cascade.

namespace
{
/// Action returning a callable taking an int and returning void - ap must reject it.
struct VoidFnAction
{
    auto operator()() const
    {
        return [](int) {};
    }
};
}  // namespace

void ap_returns_void_pair()
{
    fnfelt::monad::io::IO<VoidFnAction> const fn_io{VoidFnAction{}};
    fnfelt::monad::io::IO<int (*)()> const value_io{[] { return 1; }};
    auto const applied = fnfelt::monad::io::ap(fn_io, value_io);
}
