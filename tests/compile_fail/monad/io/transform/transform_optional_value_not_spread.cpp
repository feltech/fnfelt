// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <optional>

#include <fnfelt/monad/io.hpp>

// The source IO produces std::optional<int>, which has one type template argument but is not
// tuple-like. A transformer taking exactly (int) passed the old structural spread rule, but
// std::apply can never unpack an optional, so acceptance deferred to a standard-library hard error.
// Spread is now restricted to std::pair/std::tuple, so validate_transform must reject this at the
// friendly static_assert boundary.

void apply_transformer()
{
    auto const source_io = fnfelt::monad::io::just(std::optional<int>{1});
    auto const transformed = source_io.transform([](int) { return 2; });
    (void)transformed;
}
