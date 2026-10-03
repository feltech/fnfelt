// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <type_traits>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Move-only functor action returning a callable - must be moved, not copied, into the IO.
struct MoveOnlyFnAction
{
    std::unique_ptr<int> ptr{std::make_unique<int>(2)};

    MoveOnlyFnAction() = default;
    MoveOnlyFnAction(MoveOnlyFnAction &&) = default;
    MoveOnlyFnAction & operator=(MoveOnlyFnAction &&) = delete;
    auto operator()() const
    {
        return [multiplier = *ptr](int x) { return x * multiplier; };
    }
};

/// Move-only functor action returning a value - must be moved, not copied, into the IO.
struct MoveOnlyValueAction
{
    std::unique_ptr<int> ptr{std::make_unique<int>(10)};

    MoveOnlyValueAction() = default;
    MoveOnlyValueAction(MoveOnlyValueAction &&) = default;
    MoveOnlyValueAction & operator=(MoveOnlyValueAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline applying a function IO to a value IO via ap.
consteval int constexpr_pipeline_ap()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied();
}

/// Constant-evaluated IO pipeline composing ap with a subsequent bind.
consteval int constexpr_pipeline_ap_bind()
{
    constexpr auto bound =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
            fnfelt::monad::io::create([] { return 1; }))
            .bind([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound();
}

/// Constant-evaluated IO pipeline applying a function IO to a value IO via the named ap overload.
consteval int constexpr_pipeline_ap_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto applied = fnfelt::monad::io::ap<"Named ap"_ss>(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied();
}

/// Constant-evaluated IO pipeline applying a function IO to a value IO via the explicit-traits ap
/// overload.
consteval int constexpr_pipeline_ap_traits()
{
    constexpr auto applied = fnfelt::monad::io::ap<CustomIOTraits>(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied();
}

/// Named function used to build an IO result from an ap application.
int int_action()
{
    return 1;
}
}  // namespace

TEST_CASE("detail::maybe_ap_result_name reflects an ap application's result")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::maybe_ap_result_name;

    using value_io_type = IO<int (*)()>;
    using fn_io_type = IO<decltype([] { return [](int x) { return x + 1; }; })>;
    // Plain-value result: shown by display string.
    static_assert(maybe_ap_result_name<^^fn_io_type, ^^value_io_type>() == "int");
    // Void result is still determinable.
    using void_fn_io_type = IO<decltype([] { return [](int) {}; })>;
    static_assert(maybe_ap_result_name<^^void_fn_io_type, ^^value_io_type>() == "void");
    // IO result: shown by the result IO's name rather than a display string.
    using result_io_type = IO<int (*)()>;
    using io_fn_io_type =
        IO<decltype([] { return [](int) { return result_io_type{&int_action}; }; })>;
    static_assert(maybe_ap_result_name<^^io_fn_io_type, ^^value_io_type>() == "IO");
    // Incompatible function value: no result determinable.
    using string_fn_io_type = IO<decltype([] { return [](std::string) { return 1; }; })>;
    static_assert(maybe_ap_result_name<^^string_fn_io_type, ^^value_io_type>() == "<unknown>");
    // Incomplete function value: no result determinable.
    using incomplete_fn_io_type = IO<Incomplete (*)()>;
    static_assert(maybe_ap_result_name<^^incomplete_fn_io_type, ^^value_io_type>() == "<unknown>");
    // Non-IO argument: no result determinable.
    static_assert(maybe_ap_result_name<^^int, ^^value_io_type>() == "<unknown>");
    static_assert(maybe_ap_result_name<^^fn_io_type, ^^int>() == "<unknown>");
}

TEST_CASE("ap applies the function IO's value to the value IO's value")
{
    using fnfelt::monad::io::IO;

    auto const result = fnfelt::monad::io::ap(
        IO{[] { return [](int x) { return x + 1; }; }}, IO{[] { return 41; }});
    CHECK_EQ(result(), 42);
}

TEST_CASE("ap produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_ap() == 42);
    static_assert(constexpr_pipeline_ap_bind() == 20);
    static_assert(constexpr_pipeline_ap_named() == 42);
    static_assert(constexpr_pipeline_ap_traits() == 42);
}

TEST_CASE("ap defaults to IOTraits for the result, not the argument IOs' traits")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Both IOs have custom traits: the result takes the default IOTraits, not either argument's.
    auto const fn_action = [] { return [](int x) { return x + 1; }; };
    auto const value_action = [] { return 1; };
    auto const applied = fnfelt::monad::io::ap(
        IO<decltype(fn_action), CustomIOTraits>{fn_action},
        IO<decltype(value_action), CustomIOTraits>{value_action});
    static_assert(std::is_same_v<decltype(applied)::traits, IOTraits>);
    CHECK_EQ(applied(), 2);
}

TEST_CASE("ap with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    auto const result = fnfelt::monad::io::ap<"Named"_ss>(
        IO{[] { return [](int x) { return x + 1; }; }}, IO{[] { return 41; }});
    static_assert(decltype(result)::traits::name == "Named");
    CHECK_EQ(result(), 42);
}

TEST_CASE("ap with explicit traits uses those traits for the result and validation")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Both IOs have default traits, but the explicit traits override them for the result.
    auto const result = fnfelt::monad::io::ap<CustomIOTraits>(
        IO{[] { return [](int x) { return x * 2; }; }}, IO{[] { return 21; }});
    static_assert(std::is_same_v<decltype(result)::traits, CustomIOTraits>);
    CHECK_EQ(result(), 42);

    // The function IO's custom traits do not leak into the result when overridden.
    auto const fn_action = [] { return [](int x) { return x + 1; }; };
    auto const overridden = fnfelt::monad::io::ap<IOTraits>(
        IO<decltype(fn_action), CustomIOTraits>{fn_action}, IO{[] { return 1; }});
    static_assert(std::is_same_v<decltype(overridden)::traits, IOTraits>);
    CHECK_EQ(overridden(), 2);
}

TEST_CASE("ap copies lvalue IOs and moves rvalue IOs")
{
    using fnfelt::monad::io::IO;

    auto const fn_io = IO{[] { return [](int x) { return x + 1; }; }};
    auto const value_io = IO{[] { return 1; }};
    auto const copied = fnfelt::monad::io::ap(fn_io, value_io);
    CHECK_EQ(copied(), 2);
    // The lvalues are untouched, so they remain usable.
    CHECK_EQ(fn_io()(1), 2);
    CHECK_EQ(value_io(), 1);

    auto const moved = fnfelt::monad::io::ap(IO{MoveOnlyFnAction{}}, IO{MoveOnlyValueAction{}});
    CHECK_EQ(moved(), 20);
    static_assert(!std::is_copy_constructible_v<decltype(moved)>);
}

TEST_CASE("ap does not spread tuple-like values")
{
    using fnfelt::monad::io::IO;

    // The callable takes the whole pair as a single argument and is applied directly.
    auto const applied = fnfelt::monad::io::ap(
        IO{[]
           {
               return [](std::pair<int, int> pair_value)
               { return pair_value.first + pair_value.second; };
           }},
        IO{[] { return std::pair{1, 2}; }});
    CHECK_EQ(applied(), 3);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
