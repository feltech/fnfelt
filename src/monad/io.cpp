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
using fnfelt::monad::io::ap;
using fnfelt::monad::io::create;
using IOTraits = fnfelt::monad::io::IOTraits<>;

/// Constant-evaluated IO pipeline using the umbrella header end-to-end: create, member bind,
/// then ap, locking the umbrella's completeness.
consteval int constexpr_pipeline_umbrella()
{
    constexpr auto bound =
        create([] { return 1; }).and_then([](int x) { return create([x] { return x + 1; }); });
    constexpr auto applied = ap(
        create([] { return [](int x) { return x * 10; }; }), create([bound] { return bound(); }));
    return applied();
}
}  // namespace

TEST_CASE("The io.hpp umbrella provides the full public interface")
{
    // create: default-traits overload names the IO "IO".
    auto const source = create([] { return 1; });
    static_assert(std::is_same_v<decltype(source)::traits, IOTraits>);

    // Member and_then, free and_then and free ap all resolve through the umbrella alone.
    auto const bound = source.and_then([](int x) { return create([x] { return x + 1; }); });
    auto const free_bound = and_then(source, [](int x) { return create([x] { return x + 1; }); });
    auto const applied =
        ap(create([] { return [](int x) { return x * 10; }; }), create([] { return 2; }));

    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(free_bound)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(applied)::traits, IOTraits>);

    CHECK_EQ(bound(), 2);
    CHECK_EQ(free_bound(), 2);
    CHECK_EQ(applied(), 20);
}

TEST_CASE("The umbrella header supports a constexpr pipeline end-to-end")
{
    static_assert(constexpr_pipeline_umbrella() == 20);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
