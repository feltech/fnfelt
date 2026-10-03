// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io.hpp>

#include <doctest/doctest.h>

#include <type_traits>

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
using fnfelt::monad::io::and_then;
using fnfelt::monad::io::create;
using IOTraits = fnfelt::monad::io::IOTraits<>;
}  // namespace

TEST_CASE("The io.hpp umbrella provides the full public interface")
{
    // create: default-traits overload names the IO "IO".
    auto const source = create([] { return 1; });
    static_assert(std::is_same_v<decltype(source)::traits, IOTraits>);

    // Member and_then, free and_then and free ap all resolve through the umbrella alone.
    auto const bound = source.and_then([](int x) { return create([x] { return x + 1; }); });
    auto const free_bound = and_then(source, [](int x) { return create([x] { return x + 1; }); });

    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(free_bound)::traits, IOTraits>);

    CHECK_EQ(bound(), 2);
    CHECK_EQ(free_bound(), 2);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
