// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <memory>
#include <tuple>
#include <type_traits>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Move-only kleisli that reads through a unique_ptr as a const lvalue - passes validation.
struct MoveOnlyKleisli
{
    std::unique_ptr<int> ptr{std::make_unique<int>(2)};

    auto operator()(int value) const
    {
        return fnfelt::monad::io::IO{[this, value] { return value + *ptr; }};
    }
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline chaining two binds.
consteval int constexpr_pipeline_bind()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .bind([](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); })
            .bind([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound();
}

/// Constant-evaluated IO pipeline binding via the named bind overload.
consteval int constexpr_pipeline_bind_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .template bind<"Named bind"_ss>(
                [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound();
}

/// Constant-evaluated IO pipeline binding via the explicit-traits bind overload.
consteval int constexpr_pipeline_bind_traits()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .template bind<CustomIOTraits>(
                [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound();
}

/// Constant-evaluated IO pipeline binding via the free bind function.
consteval int constexpr_pipeline_bind_free()
{
    constexpr auto bound = fnfelt::monad::io::bind(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound();
}

/// Constant-evaluated IO pipeline binding via the free named bind function.
consteval int constexpr_pipeline_bind_free_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto bound = fnfelt::monad::io::bind<"Named free bind"_ss>(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound();
}

/// Constant-evaluated IO pipeline binding via the free explicit-traits bind function.
consteval int constexpr_pipeline_bind_free_traits()
{
    constexpr auto bound = fnfelt::monad::io::bind<CustomIOTraits>(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound();
}

/// Constant-evaluated IO pipeline spreading a pair value into the kleisli.
consteval int constexpr_pipeline_spread()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return std::pair{1, 2}; })
            .bind([](int x, int y) { return fnfelt::monad::io::create([x, y] { return x + y; }); });
    return bound();
}
}  // namespace

TEST_CASE("IO bind accepts a direct kleisli and runs source then continuation")
{
    using fnfelt::monad::io::IO;

    auto const bound = IO{[] { return 1; }}.bind([](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(bound(), 2);
}

TEST_CASE("IO bind spreads pair and tuple values into the kleisli")
{
    using fnfelt::monad::io::IO;

    auto const bound_pair = IO{[] { return std::pair{1, 2}; }}.bind(
        [](int x, int y) { return IO{[x, y] { return x + y; }}; });
    CHECK_EQ(bound_pair(), 3);

    auto const bound_tuple = IO{[] { return std::tuple{3, 4}; }}.bind(
        [](int x, int y) { return IO{[x, y] { return x * y; }}; });
    CHECK_EQ(bound_tuple(), 12);
}

TEST_CASE("IO bind prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The kleisli accepts the whole pair, so it must be invoked directly even though the value
    // could also be spread.
    auto const bound = IO{[] { return std::pair{1, 2}; }}.bind(
        [](std::pair<int, int> pair_value)
        { return IO{[pair_value] { return pair_value.first * 10 + pair_value.second; }}; });
    CHECK_EQ(bound(), 12);
}

TEST_CASE("IO bind chains through successive continuations")
{
    using fnfelt::monad::io::IO;

    auto const bound = IO{[] { return 1; }}
                           .bind([](int x) { return IO{[x] { return x + 1; }}; })
                           .bind([](int x) { return IO{[x] { return x * 10; }}; });
    CHECK_EQ(bound(), 20);
}

TEST_CASE("IO bind accepts an lvalue kleisli and copies it")
{
    using fnfelt::monad::io::IO;

    // A named copyable kleisli must be accepted (copied by value), not deduced as a reference
    // type and rejected.
    struct IdentityKleisli
    {
        int offset;

        auto operator()(int value) const
        {
            return IO{[value, offset = offset] { return value + offset; }};
        }
    };

    IdentityKleisli kleisli_lvalue{10};
    auto const bound = IO{[] { return 1; }}.bind(kleisli_lvalue);
    CHECK_EQ(bound(), 11);

    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(kleisli_lvalue(2)(), 12);
}

TEST_CASE("IO bind works on a const IO and with a move-only kleisli")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 5; }};
    auto const bound = source.bind(MoveOnlyKleisli{});
    CHECK_EQ(bound(), 7);
}

TEST_CASE("IO with custom traits binds and runs end-to-end")
{
    using fnfelt::monad::io::IO;

    struct CustomAction
    {
        int value;

        int operator()() const
        {
            return value;
        }
    };

    IO const source{[] { return 1; }};
    auto const bound =
        source.bind([](int x) { return IO<CustomAction, CustomIOTraits>{CustomAction{x + 1}}; });
    CHECK_EQ(bound(), 2);
}

TEST_CASE("bind member defaults to IOTraits for the result")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Source IO with custom traits: the result takes the default IOTraits, not the source's.
    IO const source{[] { return 1; }};
    auto const bound = source.bind([](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    CHECK_EQ(bound(), 2);
}

TEST_CASE("bind member with explicit traits uses those traits for the result")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 1; }};
    auto const bound =
        source.template bind<CustomIOTraits>([](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(std::is_same_v<decltype(bound)::traits, CustomIOTraits>);
    CHECK_EQ(bound(), 2);
}

TEST_CASE("bind member with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    IO const source{[] { return 1; }};
    auto const bound =
        source.template bind<"Named bind"_ss>([](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(decltype(bound)::traits::name == "Named bind");
    CHECK_EQ(bound(), 2);
}

TEST_CASE("free bind uses the default, named and explicit-traits forms")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;
    using namespace fnfelt::literals;  // NOLINT

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const bound = fnfelt::monad::io::bind(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    CHECK_EQ(bound(), 2);

    auto const named = fnfelt::monad::io::bind<"Named free bind"_ss>(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(decltype(named)::traits::name == "Named free bind");
    CHECK_EQ(named(), 2);

    auto const trait_bound = fnfelt::monad::io::bind<CustomIOTraits>(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(std::is_same_v<decltype(trait_bound)::traits, CustomIOTraits>);
    CHECK_EQ(trait_bound(), 2);
}

TEST_CASE("bind produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_bind() == 20);
    static_assert(constexpr_pipeline_bind_named() == 2);
    static_assert(constexpr_pipeline_bind_traits() == 2);
    static_assert(constexpr_pipeline_bind_free() == 2);
    static_assert(constexpr_pipeline_bind_free_named() == 2);
    static_assert(constexpr_pipeline_bind_free_traits() == 2);
    static_assert(constexpr_pipeline_spread() == 3);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
