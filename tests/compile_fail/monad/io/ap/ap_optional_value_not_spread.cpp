// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <optional>

#include <fnfelt/monad/io.hpp>

// The value IO produces std::optional<int>, which has one type template argument but is not
// tuple-like. A callable taking exactly (int) passed the old structural spread rule, but std::apply
// can never unpack an optional, so acceptance deferred to a standard-library hard error. Spread is
// now restricted to std::pair/std::tuple, so validate_ap must reject this at the friendly
// static_assert boundary.

void apply_fn()
{
    auto const fn_io = fnfelt::monad::io::just([](int) { return 1; });
    auto const value_io = fnfelt::monad::io::just(std::optional<int>{1});
    auto const applied = fnfelt::monad::io::ap(fn_io, value_io);
    (void)applied;
}
