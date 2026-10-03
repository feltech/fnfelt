// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <string>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/ap.hpp>

// ap with default traits must reject a function IO whose value (a callable taking a
// std::string) is not callable with the value IO's value (an int), failing with the friendly
// static_assert message rather than a template error cascade.

namespace
{
/// Action returning a callable taking a std::string - incompatible with an int value.
struct StringFnAction
{
    auto operator()() const
    {
        return [](std::string) { return 1; };
    }
};
}  // namespace

void ap_incompatible_pair()
{
    fnfelt::monad::io::IO<StringFnAction> const fn_io{StringFnAction{}};
    fnfelt::monad::io::IO<int (*)()> const value_io{[] { return 1; }};
    auto const applied = fnfelt::monad::io::ap(fn_io, value_io);
}
