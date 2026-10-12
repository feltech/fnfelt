// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io.hpp>

// The source is an async leaf and the continuation is a valid callable, but it returns a non-IO.
// The default validate_and_then must reject it with the friendly static_assert reason, and the
// diagnostic path must see through the async proxy to the unwrapped value type without blowing up.

void apply_continuation()
{
    using namespace fnfelt::literals;  // NOLINT

    auto const source_io = fnfelt::monad::io::create_async<"AsyncSource"_ss>(
        [](auto, int x) -> lf::task<int> { co_return x * 2; }, 21);
    auto const bound = source_io.and_then([](int) { return 1; });
    (void)bound;
}
