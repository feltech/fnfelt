// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io.hpp>

// The free bind is called with a non-IO source (an int) and a valid kleisli, so the catch-all
// overload must be selected and fail with the friendly static_assert message naming the source as
// not an IO. Including only the umbrella header also exercises its completeness.

void bind_non_io()
{
    auto const bound = fnfelt::monad::io::bind(
        42, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    (void)bound;
}
