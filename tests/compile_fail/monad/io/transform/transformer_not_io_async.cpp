// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io.hpp>

// The source is an async leaf and the transformer is a valid callable, but it returns an IO rather
// than a plain value. The default validate_transform must reject it with the friendly
// static_assert reason, and the diagnostic path must see through the async proxy to the unwrapped
// value type without blowing up.

void apply_transformer()
{
    using namespace fnfelt::literals;  // NOLINT

    auto const source_io = fnfelt::monad::io::create_async<"AsyncSource"_ss>(
        [](auto, int x) -> lf::task<int> { co_return x * 2; }, 21);
    auto const transformed = source_io.transform(
        [](int x) { return fnfelt::monad::io::create<"ResultIO"_ss>([x] { return x + 1; }); });
    (void)transformed;
}
