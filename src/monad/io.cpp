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
using fnfelt::monad::io::transform;
using IOTraits = fnfelt::monad::io::IOTraits<>;

/// Constant-evaluated IO pipeline using the umbrella header end-to-end: create, member transform,
/// member and_then, then ap, locking the umbrella's completeness.
consteval int constexpr_pipeline_umbrella()
{
    constexpr auto transformed = create([] { return 1; }).transform([](int x) { return x + 1; });
    constexpr auto bound =
        transformed.and_then([](int x) { return create([x] { return x + 1; }); });
    constexpr auto applied =
        ap(create([] { return [](int x) { return x * 10; }; }),
           create([bound] { return bound().sync_wait(); }));
    return applied().sync_wait();
}
}  // namespace

TEST_CASE("The io.hpp umbrella provides the full public interface")
{
    // create: default-traits overload names the IO "IO".
    auto const source = create([] { return 1; });
    static_assert(std::is_same_v<decltype(source)::traits, IOTraits>);

    // Member and_then, free and_then, member transform, free transform and free ap all resolve
    // through the umbrella alone.
    auto const bound = source.and_then([](int x) { return create([x] { return x + 1; }); });
    auto const free_bound = and_then(source, [](int x) { return create([x] { return x + 1; }); });
    auto const transformed = source.transform([](int x) { return x + 1; });
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    auto const free_transformed = transform(source, [](int x) { return x + 1; });
    auto const applied =
        ap(create([] { return [](int x) { return x * 10; }; }), create([] { return 2; }));

    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(free_bound)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(transformed)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(free_transformed)::traits, IOTraits>);
    static_assert(std::is_same_v<decltype(applied)::traits, IOTraits>);

    CHECK_EQ(bound().sync_wait(), 2);
    CHECK_EQ(free_bound().sync_wait(), 2);
    CHECK_EQ(transformed().sync_wait(), 2);
    CHECK_EQ(free_transformed().sync_wait(), 2);
    CHECK_EQ(applied().sync_wait(), 20);
}

TEST_CASE("The umbrella header supports a constexpr pipeline end-to-end")
{
    static_assert(constexpr_pipeline_umbrella() == 30);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
