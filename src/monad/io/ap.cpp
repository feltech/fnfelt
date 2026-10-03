// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

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

/// Move-only functor action returning a pair with a move-only element.
struct MoveOnlyPairAction
{
    std::unique_ptr<int> ptr{std::make_unique<int>(20)};

    MoveOnlyPairAction() = default;
    MoveOnlyPairAction(MoveOnlyPairAction &&) = default;
    MoveOnlyPairAction & operator=(MoveOnlyPairAction &&) = delete;
    auto operator()() const
    {
        return std::pair{2, std::make_unique<int>(*ptr)};
    }
};

/// Move-only value action incrementing a move counter on every move - pins that rvalue chains move.
struct MoveTrackingValueAction
{
    /// Number of move constructions observed since the last reset.
    static inline int moves = 0;
    std::unique_ptr<int> ptr{std::make_unique<int>(10)};

    MoveTrackingValueAction() = default;
    MoveTrackingValueAction(MoveTrackingValueAction && other) noexcept : ptr{std::move(other.ptr)}
    {
        ++moves;
    }
    MoveTrackingValueAction & operator=(MoveTrackingValueAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

/// Overloaded callable: the direct (pair-taking) overload must win over the spread one.
struct OverloadedFn
{
    int operator()(std::pair<int, int> pair_value) const
    {
        return pair_value.first * 10 + pair_value.second;
    }

    int operator()(int lhs, int rhs) const
    {
        return lhs * 100 + rhs;
    }
};

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

/// Constant-evaluated IO pipeline spreading a pair value into the callable.
consteval int constexpr_pipeline_ap_spread()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int lhs, int rhs) { return lhs * rhs; }; }),
        fnfelt::monad::io::create([] { return std::pair{6, 7}; }));
    return applied();
}

/// Constant-evaluated IO pipeline composing ap with a subsequent and_then.
consteval int constexpr_pipeline_ap_and_then()
{
    constexpr auto bound =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
            fnfelt::monad::io::create([] { return 1; }))
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
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

namespace ap_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::IO;
using fnfelt::monad::io::IOTraits;

/// Function IO action returning a callable accepting the value IO's `int` - compatible pair.
struct FnAction
{
    auto operator()() const
    {
        return [](int x) { return x + 1; };
    }
};

/// Value IO action returning an `int` - the compatible pair's argument.
struct ValueAction
{
    int operator()() const
    {
        return 1;
    }
};

/// Function IO action whose callable accepts a `std::string`, not the value IO's `int`.
struct IncompatibleFnAction
{
    auto operator()() const
    {
        return [](std::string) { return 1; };
    }
};

using FnIO = IO<FnAction, IOTraits<"FnIO"_ss>>;
using ValueIO = IO<ValueAction, IOTraits<"ValueIO"_ss>>;
using IncompatibleFnIO = IO<IncompatibleFnAction, IOTraits<"FnIO"_ss>>;
}  // namespace ap_error_msg_test

TEST_CASE("detail::ap_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::ap_error_msg;

    // Valid function IO and value IO with a determinable result: both IO names are shown, then the
    // reason, then the offending type (the function IO, whose display string is appended).
    static_assert(
        ap_error_msg<^^ap_error_msg_test::FnIO, ^^ap_error_msg_test::ValueIO>(
            "ApIO", "custom reason") ==
        std::string{"fnfelt: IO ap error: ApIO{(FnIO()(ValueIO()) => int)}: custom reason: "} +
            std::string{display_string_of(^^ap_error_msg_test::FnIO)});

    // Non-IO function: no IO names are determinable, and the offending type is the function.
    static_assert(
        ap_error_msg<^^int, ^^ap_error_msg_test::ValueIO>("ApIO", "custom reason") ==
        "fnfelt: IO ap error: ApIO{(<unknown>()(ValueIO()) => <unknown>)}: custom reason: int");

    // Non-IO value with a valid function IO: the value name is not determinable, and the offending
    // type is the value.
    static_assert(
        ap_error_msg<^^ap_error_msg_test::FnIO, ^^int>("ApIO", "custom reason") ==
        "fnfelt: IO ap error: ApIO{(FnIO()(<unknown>()) => <unknown>)}: custom reason: int");

    // Result not determinable (the callable rejects the value): the result name is "<unknown>", and
    // the offending type is the function IO, whose display string is appended.
    static_assert(
        ap_error_msg<^^ap_error_msg_test::IncompatibleFnIO, ^^ap_error_msg_test::ValueIO>(
            "ApIO", "custom reason") ==
        std::string{
            "fnfelt: IO ap error: ApIO{(FnIO()(ValueIO()) => <unknown>)}: custom reason: "} +
            std::string{display_string_of(^^ap_error_msg_test::IncompatibleFnIO)});
}

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
    // Lambda result: shown by kind as "lambda" rather than a structural display string.
    using lambda_fn_io_type = IO<decltype([] { return [](int) { return [] { return 1; }; }; })>;
    static_assert(maybe_ap_result_name<^^lambda_fn_io_type, ^^value_io_type>() == "lambda");
    // Spread: two-argument callable with a pair value resolves through the spread branch.
    using spread_fn_io_type = IO<decltype([] { return [](int, int) { return 1; }; })>;
    using pair_value_io_type = IO<std::pair<int, int> (*)()>;
    static_assert(maybe_ap_result_name<^^spread_fn_io_type, ^^pair_value_io_type>() == "int");
    // Spread void result is still determinable.
    using spread_void_fn_io_type = IO<decltype([] { return [](int, int) {}; })>;
    static_assert(maybe_ap_result_name<^^spread_void_fn_io_type, ^^pair_value_io_type>() == "void");
    // Spread IO result: shown by the result IO's name.
    using spread_io_fn_io_type =
        IO<decltype([] { return [](int, int) { return result_io_type{&int_action}; }; })>;
    static_assert(maybe_ap_result_name<^^spread_io_fn_io_type, ^^pair_value_io_type>() == "IO");
    // Two-argument callable with a non-spreadable value: no result determinable.
    static_assert(maybe_ap_result_name<^^spread_fn_io_type, ^^value_io_type>() == "<unknown>");
}

TEST_CASE("ap applies the IO-wrapped function's callable to the IO-wrapped value's value")
{
    using fnfelt::monad::io::IO;

    auto const result = fnfelt::monad::io::ap(
        IO{[] { return [](int x) { return x + 1; }; }}, IO{[] { return 41; }});
    CHECK_EQ(result(), 42);
}

TEST_CASE("ap produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_ap() == 42);
    static_assert(constexpr_pipeline_ap_spread() == 42);
    static_assert(constexpr_pipeline_ap_and_then() == 20);
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

    // A named ap with an lvalue function IO and an rvalue value IO still resolves to the named IO
    // overload (the named workhorse handles both IO arguments), naming the result.
    auto const fn_io = IO{[] { return [](int x) { return x * 2; }; }};
    auto const mixed = fnfelt::monad::io::ap<"Named mixed"_ss>(fn_io, IO{[] { return 21; }});
    static_assert(decltype(mixed)::traits::name == "Named mixed");
    CHECK_EQ(mixed(), 42);
    // The lvalue function IO is untouched, so it remains usable.
    CHECK_EQ(fn_io()(2), 4);
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

TEST_CASE("ap spreads pair and tuple values into the callable")
{
    using fnfelt::monad::io::IO;

    auto const applied_pair = fnfelt::monad::io::ap(
        IO{[] { return [](int lhs, int rhs) { return lhs + rhs; }; }},
        IO{[] { return std::pair{1, 2}; }});
    CHECK_EQ(applied_pair(), 3);

    auto const applied_tuple = fnfelt::monad::io::ap(
        IO{[] { return [](int lhs, int rhs) { return lhs * rhs; }; }},
        IO{[] { return std::tuple{3, 4}; }});
    CHECK_EQ(applied_tuple(), 12);
}

TEST_CASE("ap prefers direct invocation over spreading")
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

    // An overloaded functor pins precedence at runtime: the pair-taking overload must win.
    auto const overloaded = fnfelt::monad::io::ap(
        IO{[] { return OverloadedFn{}; }}, IO{[] { return std::pair{1, 2}; }});
    CHECK_EQ(overloaded(), 12);
}

TEST_CASE("ap moves spread elements into the callable")
{
    using fnfelt::monad::io::IO;

    auto const applied = fnfelt::monad::io::ap(
        IO{[] { return [](int mult, std::unique_ptr<int> ptr) { return *ptr * mult; }; }},
        IO{MoveOnlyPairAction{}});
    CHECK_EQ(applied(), 40);
    static_assert(!std::is_copy_constructible_v<decltype(applied)>);
}

TEST_CASE("ap moves move-only rvalue IOs through a chain without copying")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    // A move-only value IO cannot be copied, so it must be moved into the ApAction member. The
    // action is move-only, hence so is the resulting IO.
    MoveTrackingValueAction::moves = 0;
    auto const applied = fnfelt::monad::io::ap(
        IO{[] { return [](int x) { return x + 1; }; }},
        IO<MoveTrackingValueAction, IOTraits<>>{MoveTrackingValueAction{}});
    CHECK_EQ(applied(), 11);
    static_assert(!std::is_copy_constructible_v<decltype(applied)>);
    // Three moves: the temporary action into the value IO's action member; the value IO into the
    // ApAction's value_io member (the forwarding reference's forwarding); and the ApAction into the
    // returned IO's action member. The rvalue temporary is elided into the IO constructor's
    // by-value parameter, so it costs no extra move.
    CHECK_EQ(MoveTrackingValueAction::moves, 3);

    // Composing an and_then moves the whole ap result into the AndThenAction's source member and
    // the new AndThenAction into the returned IO's action member, two further moves. The old
    // by-value signature additionally moved each IO into its by-value parameter on entry, costing
    // one extra move per hop.
    MoveTrackingValueAction::moves = 0;
    auto const bound = fnfelt::monad::io::ap(
                           IO{[] { return [](int x) { return x + 1; }; }},
                           IO<MoveTrackingValueAction, IOTraits<>>{MoveTrackingValueAction{}})
                           .and_then([](int x) { return IO{[x] { return x * 10; }}; });
    CHECK_EQ(bound(), 110);
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    CHECK_EQ(MoveTrackingValueAction::moves, 5);
}

TEST_CASE("ap copies lvalue IOs independently of an rvalue argument")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    // The value IO is an lvalue and must be copied; the function IO is an rvalue and must be moved
    // independently, so the move-only value IO path is not forced through a copy.
    auto const value_io = IO{[] { return 21; }};
    auto const result =
        fnfelt::monad::io::ap(IO{[] { return [](int x) { return x * 2; }; }}, value_io);
    CHECK_EQ(result(), 42);
    CHECK_EQ(value_io(), 21);

    // A move-only rvalue value IO paired with an lvalue function IO can still be moved (the free
    // `ap` forwards each argument independently), so this must compile and run.
    auto const fn_io = IO{[] { return [](int x) { return x + 1; }; }};
    auto const mixed = fnfelt::monad::io::ap(
        fn_io, IO<MoveTrackingValueAction, IOTraits<>>{MoveTrackingValueAction{}});
    CHECK_EQ(mixed(), 11);
    static_assert(!std::is_copy_constructible_v<decltype(mixed)>);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
