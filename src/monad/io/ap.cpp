// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
using CustomIOTraits = fnfelt::monad::io::ApIOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline applying a function IO to a value IO via ap.
consteval int constexpr_pipeline_ap()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied.build().sync_wait();
}

/// Constant-evaluated IO pipeline spreading a pair value into the callable.
consteval int constexpr_pipeline_ap_spread()
{
    constexpr auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int lhs, int rhs) { return lhs * rhs; }; }),
        fnfelt::monad::io::create([] { return std::pair{6, 7}; }));
    return applied.build().sync_wait();
}

/// Constant-evaluated IO pipeline composing ap with a subsequent and_then.
consteval int constexpr_pipeline_ap_and_then()
{
    constexpr auto bound =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
            fnfelt::monad::io::create([] { return 1; }))
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound.build().sync_wait();
}

/// Constant-evaluated IO pipeline applying a function IO to a value IO via the named ap overload.
consteval int constexpr_pipeline_ap_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto applied = fnfelt::monad::io::ap<"Named ap"_ss>(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied.build().sync_wait();
}

/// Constant-evaluated IO pipeline applying a function IO to a value IO via the explicit-traits ap
/// overload.
consteval int constexpr_pipeline_ap_traits()
{
    constexpr auto applied = fnfelt::monad::io::ap<CustomIOTraits>(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    return applied.build().sync_wait();
}

/// Named function used to build an IO result from an ap application.
int int_action()
{
    return 1;
}

/// Stateless functor returned by an async function leaf, incrementing its argument.
struct Increment
{
    constexpr int operator()(int value) const
    {
        return value + 1;
    }
};

/// Movable stateful functor returned by an async function leaf, scaling then adding.
struct ScaleAndAdd
{
    int scale = 0;

    constexpr int operator()(int value) const
    {
        return scale * 100 + value;
    }
};

/// Stateless async function leaf producing an `Increment` callable.
inline constexpr auto async_fn_increment = [](auto, int) -> lf::task<Increment>
{ co_return Increment{}; };

/// Stateless async function leaf producing a `ScaleAndAdd` callable.
inline constexpr auto async_fn_scale = [](auto, int scale) -> lf::task<ScaleAndAdd>
{ co_return ScaleAndAdd{scale}; };

/// Stateless async function leaf yielding its argument unchanged.
inline constexpr auto async_identity = [](auto, int value) -> lf::task<int> { co_return value; };

/// Stateless async function leaf producing a pair, for spread dispatch.
inline constexpr auto async_pair = [](auto, int value) -> lf::task<std::pair<int, int>>
{ co_return std::pair{value, value + 1}; };

/// Move-only callable holding a factor, returned by an async function leaf.
struct MoveOnlyCallable
{
    std::unique_ptr<int> factor;

    int operator()(int value) const
    {
        return value * *factor;
    }
};

/// Stateless async function leaf producing a move-only callable from a move-only factor.
inline constexpr auto async_move_only_fn =
    [](auto, std::unique_ptr<int> factor) -> lf::task<MoveOnlyCallable>
{ co_return MoveOnlyCallable{std::move(factor)}; };

/// Bound on the rendezvous spin so a serialized run cannot hang.
inline constexpr int rendezvous_spin_limit = 10000000;

/**
 * Publish this side's flag, then bounded-spin until the peer's flag is observed.
 *
 * @param self_flag Flag set by this side.
 * @param peer_flag Flag set by the other side.
 * @return True if the peer's flag was observed before the bound elapsed.
 */
bool rendezvous(std::atomic<bool> * self_flag, std::atomic<bool> * peer_flag)
{
    self_flag->store(true, std::memory_order_relaxed);
    for (int spin = 0; spin < rendezvous_spin_limit; ++spin)
    {
        if (peer_flag->load(std::memory_order_relaxed))
        {
            return true;
        }
        if (spin % 1024 == 0)
        {
            std::this_thread::yield();
        }
    }
    return false;
}

/// Callable whose result is poisoned when its rendezvous timed out.
struct RendezvousCallable
{
    bool healthy = false;

    int operator()(int value) const
    {
        return healthy ? value + 1 : -1;
    }
};

/// Callable returned by a rendezvousing sync fn side; carries a poison `add` on timeout.
struct SyncRendezvousFn
{
    int add = 0;

    int operator()(int value) const
    {
        return value + add;
    }
};

/// Stateless async function leaf that rendezvouses, then yields a callable.
inline constexpr auto async_rendezvous_fn =
    [](auto,
       std::atomic<bool> * self_flag,
       std::atomic<bool> * peer_flag) -> lf::task<RendezvousCallable>
{ co_return RendezvousCallable{rendezvous(self_flag, peer_flag)}; };

/// Stateless async value leaf that rendezvouses, then yields its value (poisoned on timeout).
inline constexpr auto async_rendezvous_value =
    [](auto, std::atomic<bool> * self_flag, std::atomic<bool> * peer_flag) -> lf::task<int>
{ co_return rendezvous(self_flag, peer_flag) ? 41 : -1000; };

/// Stateless async value leaf that publishes its flag, then throws (never reaches its co_return).
inline constexpr auto async_throwing_value = [](auto, std::atomic<bool> * flag) -> lf::task<int>
{
    flag->store(true, std::memory_order_relaxed);
    throw std::runtime_error("async value failure");
    co_return 0;
};

inline constexpr char async_ap_name[] = "AsyncAp";
using AsyncApTraits = fnfelt::monad::io::ApIOTraits<async_ap_name>;

/// Custom traits delegating to the default traits, for the async ap injection point.
struct DelegatingApTraits
{
    static constexpr std::string_view name = "DelegatingApIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_ap(
        std::meta::info fn_io_meta, std::meta::info value_io_meta)
    {
        return fnfelt::monad::io::ApIOTraits<>::validate_ap(fn_io_meta, value_io_meta);
    }
};
}  // namespace

namespace ap_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::ApIOTraits;
using fnfelt::monad::io::IO;

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

using FnIO = IO<FnAction, ApIOTraits<"FnIO"_ss>>;
using ValueIO = IO<ValueAction, ApIOTraits<"ValueIO"_ss>>;
using IncompatibleFnIO = IO<IncompatibleFnAction, ApIOTraits<"FnIO"_ss>>;
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
        fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
        fnfelt::monad::io::create([] { return 41; }));
    CHECK_EQ(result.build().sync_wait(), 42);
}

TEST_CASE("ap produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_ap() == 42);
    static_assert(constexpr_pipeline_ap_spread() == 42);
    static_assert(constexpr_pipeline_ap_and_then() == 20);
    static_assert(constexpr_pipeline_ap_named() == 42);
    static_assert(constexpr_pipeline_ap_traits() == 42);
}

TEST_CASE("ap defaults to ApIOTraits for the result, not the argument IOs' traits")
{
    using fnfelt::monad::io::IO;
    using ApIOTraits = fnfelt::monad::io::ApIOTraits<>;

    // Both IOs have custom traits: the result takes the default ApIOTraits, not either argument's.
    auto const fn_action = [] { return [](int x) { return x + 1; }; };
    auto const value_action = [] { return 1; };
    auto const applied = fnfelt::monad::io::ap(
        IO<decltype(fn_action), CustomIOTraits>{fn_action},
        IO<decltype(value_action), CustomIOTraits>{value_action});
    static_assert(std::is_same_v<decltype(applied)::traits, ApIOTraits>);
    CHECK_EQ(applied.build().sync_wait(), 2);
}

TEST_CASE("ap with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    auto const result = fnfelt::monad::io::ap<"Named"_ss>(
        fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
        fnfelt::monad::io::create([] { return 41; }));
    static_assert(decltype(result)::traits::name == "Named");
    CHECK_EQ(result.build().sync_wait(), 42);

    // A named ap with an lvalue function IO and an rvalue value IO still resolves to the named IO
    // overload (the named workhorse handles both IO arguments), naming the result.
    auto const fn_io = fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; });
    auto const mixed = fnfelt::monad::io::ap<"Named mixed"_ss>(
        fn_io, fnfelt::monad::io::create([] { return 21; }));
    static_assert(decltype(mixed)::traits::name == "Named mixed");
    CHECK_EQ(mixed.build().sync_wait(), 42);
    // The lvalue function IO is untouched, so it remains usable.
    CHECK_EQ(fn_io.build().sync_wait()(2), 4);
}

TEST_CASE("ap with explicit traits uses those traits for the result and validation")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT
    using CustomApTraits = fnfelt::monad::io::ApIOTraits<"CustomAp"_ss>;

    // Both IOs have default traits, but the explicit traits override them for the result.
    auto const result = fnfelt::monad::io::ap<CustomIOTraits>(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }),
        fnfelt::monad::io::create([] { return 21; }));
    static_assert(std::is_same_v<decltype(result)::traits, CustomIOTraits>);
    CHECK_EQ(result.build().sync_wait(), 42);

    // The function IO's custom traits do not leak into the result when overridden.
    auto const fn_action = [] { return [](int x) { return x + 1; }; };
    auto const overridden = fnfelt::monad::io::ap<CustomApTraits>(
        IO<decltype(fn_action), CustomIOTraits>{fn_action},
        fnfelt::monad::io::create([] { return 1; }));
    static_assert(std::is_same_v<decltype(overridden)::traits, CustomApTraits>);
    CHECK_EQ(overridden.build().sync_wait(), 2);
}

TEST_CASE("ap copies lvalue IOs and moves rvalue IOs")
{
    using fnfelt::monad::io::IO;

    auto const fn_io = fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; });
    auto const value_io = fnfelt::monad::io::create([] { return 1; });
    auto const copied = fnfelt::monad::io::ap(fn_io, value_io);
    CHECK_EQ(copied.build().sync_wait(), 2);
    // The lvalues are untouched, so they remain usable.
    CHECK_EQ(fn_io.build().sync_wait()(1), 2);
    CHECK_EQ(value_io.build().sync_wait(), 1);

    auto moved = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(MoveOnlyFnAction{}),
        fnfelt::monad::io::create(MoveOnlyValueAction{}));
    CHECK_EQ(std::move(moved).build().sync_wait(), 20);
    static_assert(!std::is_copy_constructible_v<decltype(moved)>);
}

TEST_CASE("ap spreads pair and tuple values into the callable")
{
    using fnfelt::monad::io::IO;

    auto const applied_pair = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int lhs, int rhs) { return lhs + rhs; }; }),
        fnfelt::monad::io::create([] { return std::pair{1, 2}; }));
    CHECK_EQ(applied_pair.build().sync_wait(), 3);

    auto const applied_tuple = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int lhs, int rhs) { return lhs * rhs; }; }),
        fnfelt::monad::io::create([] { return std::tuple{3, 4}; }));
    CHECK_EQ(applied_tuple.build().sync_wait(), 12);
}

TEST_CASE("ap prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The callable takes the whole pair as a single argument and is applied directly.
    auto const applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(
            []
            {
                return [](std::pair<int, int> pair_value)
                { return pair_value.first + pair_value.second; };
            }),
        fnfelt::monad::io::create([] { return std::pair{1, 2}; }));
    CHECK_EQ(applied.build().sync_wait(), 3);

    // An overloaded functor pins precedence at runtime: the pair-taking overload must win.
    auto const overloaded = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return OverloadedFn{}; }),
        fnfelt::monad::io::create([] { return std::pair{1, 2}; }));
    CHECK_EQ(overloaded.build().sync_wait(), 12);
}

TEST_CASE("ap moves spread elements into the callable")
{
    using fnfelt::monad::io::IO;

    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(
            [] { return [](int mult, std::unique_ptr<int> ptr) { return *ptr * mult; }; }),
        fnfelt::monad::io::create(MoveOnlyPairAction{}));
    CHECK_EQ(std::move(applied).build().sync_wait(), 40);
    static_assert(!std::is_copy_constructible_v<decltype(applied)>);
}

TEST_CASE("ap moves move-only rvalue IOs through a chain without copying")
{
    using fnfelt::monad::io::ApIOTraits;
    using fnfelt::monad::io::IO;

    // A move-only value IO cannot be copied, so it must be moved into the ApAction member. The
    // action is move-only, hence so is the resulting IO.
    MoveTrackingValueAction::moves = 0;
    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
        IO<MoveTrackingValueAction, ApIOTraits<>>{MoveTrackingValueAction{}});
    CHECK_EQ(std::move(applied).build().sync_wait(), 11);
    static_assert(!std::is_copy_constructible_v<decltype(applied)>);
    // Five moves. Construction: the temporary action into the value IO's action member; the value
    // IO into the ApAction's value_io member; and the ApAction into the returned IO's action member
    // (three). Running: the IO's action into the RunProxy by value; and the value IO into its own
    // RunProxy when the ApAction runs (two). The rvalue temporary is elided into the IO
    // constructor's by-value parameter, so it costs no extra move.
    CHECK_EQ(MoveTrackingValueAction::moves, 5);

    // Composing an and_then moves the whole ap result into the AndThenAction's source member and
    // the new AndThenAction into the returned IO's action member, two further moves to construct.
    // Running adds one more nested source-IO move for the extra hop, so eight in total.
    MoveTrackingValueAction::moves = 0;
    auto bound =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
            IO<MoveTrackingValueAction, ApIOTraits<>>{MoveTrackingValueAction{}})
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    CHECK_EQ(std::move(bound).build().sync_wait(), 110);
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    CHECK_EQ(MoveTrackingValueAction::moves, 8);
}

TEST_CASE("ap copies lvalue IOs independently of an rvalue argument")
{
    using fnfelt::monad::io::ApIOTraits;
    using fnfelt::monad::io::IO;

    // The value IO is an lvalue and must be copied; the function IO is an rvalue and must be moved
    // independently, so the move-only value IO path is not forced through a copy.
    auto const value_io = fnfelt::monad::io::create([] { return 21; });
    auto const result = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x * 2; }; }), value_io);
    CHECK_EQ(result.build().sync_wait(), 42);
    CHECK_EQ(value_io.build().sync_wait(), 21);

    // A move-only rvalue value IO paired with an lvalue function IO can still be moved (the free
    // `ap` forwards each argument independently), so this must compile and run.
    auto const fn_io = fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; });
    auto mixed = fnfelt::monad::io::ap(
        fn_io, IO<MoveTrackingValueAction, ApIOTraits<>>{MoveTrackingValueAction{}});
    CHECK_EQ(std::move(mixed).build().sync_wait(), 11);
    static_assert(!std::is_copy_constructible_v<decltype(mixed)>);
}

TEST_CASE("ap composes two async sides and applies the callable")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create_async(async_fn_increment, 0),
        fnfelt::monad::io::create_async(async_identity, 41));
    // Both sides are async, so the composition must take the async arm.
    static_assert(action_is_async(^^decltype(applied)::action_type));
    static_assert(std::is_same_v<decltype(applied)::value_type, int>);
    CHECK_EQ(applied.build().sync_wait(), 42);
    CHECK_EQ(applied.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ap overlaps an async side with a sync side")
{
    using fnfelt::monad::io::detail::action_is_async;

    // Async function side, sync value side.
    auto async_fn_sync_value = fnfelt::monad::io::ap(
        fnfelt::monad::io::create_async(async_fn_increment, 0),
        fnfelt::monad::io::create([] { return 41; }));
    static_assert(action_is_async(^^decltype(async_fn_sync_value)::action_type));
    CHECK_EQ(async_fn_sync_value.build().sync_wait(lf::lazy_pool{2}), 42);

    // Sync function side, async value side.
    auto sync_fn_async_value = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
        fnfelt::monad::io::create_async(async_identity, 41));
    static_assert(action_is_async(^^decltype(sync_fn_async_value)::action_type));
    CHECK_EQ(sync_fn_async_value.build().sync_wait(lf::lazy_pool{2}), 42);

    // Negative witness: a fully sync ap stays on the sync arm.
    auto sync_ap = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int x) { return x + 1; }; }),
        fnfelt::monad::io::create([] { return 41; }));
    static_assert(!action_is_async(^^decltype(sync_ap)::action_type));
    CHECK_EQ(sync_ap.build().sync_wait(), 42);
}

TEST_CASE("ap runs both async sides concurrently (atomic rendezvous)")
{
    // The flags live in this frame, which outlives sync_wait; the stateless leaves receive
    // pointers to them. Each leaf sets its own flag, then bounded-spins until it observes the
    // other's. If the two sides were serialized on one thread, one spin would time out and its
    // poison value would flow through, failing the check.
    std::atomic<bool> fn_flag{false};
    std::atomic<bool> value_flag{false};

    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create_async(async_rendezvous_fn, &fn_flag, &value_flag),
        fnfelt::monad::io::create_async(async_rendezvous_value, &value_flag, &fn_flag));
    CHECK_EQ(applied.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ap overlaps a sync fn side with an async value side (atomic rendezvous)")
{
    // The sync fn side is lifted into a forked task, so it runs on another worker while the async
    // value side runs inline. The flags live in this frame, which outlives sync_wait; the sync
    // action captures pointers to them and the stateless leaf receives pointers too. Each side sets
    // its own flag, then bounded-spins until it observes the other's. If the two sides were
    // serialized, one spin would time out and its poison would flow through, failing the check.
    std::atomic<bool> fn_flag{false};
    std::atomic<bool> value_flag{false};

    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(
            [&fn_flag, &value_flag]
            { return SyncRendezvousFn{rendezvous(&fn_flag, &value_flag) ? 1 : -1000}; }),
        fnfelt::monad::io::create_async(async_rendezvous_value, &value_flag, &fn_flag));
    CHECK_EQ(applied.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ap propagates an exception thrown by a sync fn side")
{
    // The sync fn side is forked, so the exception crosses the fork boundary and the eventually
    // slot before being rethrown by lf::sync_wait. The declared non-void return type keeps the
    // action from producing void even though it never returns.
    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(
            []() -> Increment { throw std::runtime_error("sync fn failure"); }),
        fnfelt::monad::io::create_async(async_identity, 41));
    CHECK_THROWS_AS(
        static_cast<void>(applied.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
}

TEST_CASE("ap propagates an exception thrown by an async value side while the fn side is forked")
{
    // The value side is called inline while the sync fn side runs forked, so the two overlap. The
    // value leaf publishes its flag and then throws; the fn side spins only until that flag, then
    // returns, so the forked child completes and the join surfaces the value side's exception. Were
    // the exception surfaced before the join (the old `lf::just` shape), the fn sibling would still
    // be spinning when the task threw.
    std::atomic<bool> value_flag{false};

    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create(
            [&value_flag]
            {
                for (int spin = 0; spin < rendezvous_spin_limit; ++spin)
                {
                    if (value_flag.load(std::memory_order_relaxed))
                    {
                        return SyncRendezvousFn{1};
                    }
                    if (spin % 1024 == 0)
                    {
                        std::this_thread::yield();
                    }
                }
                return SyncRendezvousFn{-1000};
            }),
        fnfelt::monad::io::create_async(async_throwing_value, &value_flag));
    CHECK_THROWS_AS(
        static_cast<void>(applied.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
}

TEST_CASE("ap moves a move-only callable produced by an async function IO")
{
    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create_async(async_move_only_fn, std::make_unique<int>(2)),
        fnfelt::monad::io::create_async(async_identity, 21));
    static_assert(std::is_same_v<decltype(applied)::value_type, int>);
    // The function IO stores a move-only argument, so the composed action and IO are move-only.
    static_assert(!std::is_copy_constructible_v<decltype(applied)>);
    CHECK_EQ(std::move(applied).build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ap composes with and_then across alternating sync and async levels")
{
    using fnfelt::monad::io::detail::action_is_async;

    // Outer async ap: Increment(async 1) = 2. Middle sync and_then: *10 = 20. Inner async ap:
    // ScaleAndAdd{20}(async 7) = 2007.
    auto pipeline =
        fnfelt::monad::io::ap(
            fnfelt::monad::io::create_async(async_fn_increment, 0),
            fnfelt::monad::io::create_async(async_identity, 1))
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); })
            .and_then(
                [](int x)
                {
                    return fnfelt::monad::io::ap(
                        fnfelt::monad::io::create_async(async_fn_scale, x),
                        fnfelt::monad::io::create_async(async_identity, 7));
                });
    static_assert(action_is_async(^^decltype(pipeline)::action_type));
    CHECK_EQ(pipeline.build().sync_wait(lf::lazy_pool{2}), 2007);
}

TEST_CASE("ap spreads a pair produced by an async value IO")
{
    auto applied = fnfelt::monad::io::ap(
        fnfelt::monad::io::create([] { return [](int lhs, int rhs) { return lhs * 10 + rhs; }; }),
        fnfelt::monad::io::create_async(async_pair, 3));
    static_assert(std::is_same_v<decltype(applied)::value_type, int>);
    CHECK_EQ(applied.build().sync_wait(lf::lazy_pool{2}), 34);
}

TEST_CASE("ap names and traits the result on the async path")
{
    using namespace fnfelt::literals;  // NOLINT
    using fnfelt::monad::io::detail::action_is_async;

    auto named = fnfelt::monad::io::ap<"Async ap"_ss>(
        fnfelt::monad::io::create_async(async_fn_increment, 0),
        fnfelt::monad::io::create([] { return 41; }));
    static_assert(decltype(named)::traits::name == "Async ap");
    static_assert(action_is_async(^^decltype(named)::action_type));
    CHECK_EQ(named.build().sync_wait(lf::lazy_pool{2}), 42);

    auto custom = fnfelt::monad::io::ap<AsyncApTraits>(
        fnfelt::monad::io::create_async(async_fn_increment, 0),
        fnfelt::monad::io::create([] { return 41; }));
    static_assert(std::is_same_v<decltype(custom)::traits, AsyncApTraits>);
    static_assert(action_is_async(^^decltype(custom)::action_type));
    CHECK_EQ(custom.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ap consults custom traits that delegate to the default on the async path")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto applied = fnfelt::monad::io::ap<DelegatingApTraits>(
        fnfelt::monad::io::create_async(async_fn_increment, 0),
        fnfelt::monad::io::create_async(async_identity, 41));
    static_assert(std::is_same_v<decltype(applied)::traits, DelegatingApTraits>);
    static_assert(action_is_async(^^decltype(applied)::action_type));
    CHECK_EQ(applied.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("ApIOTraits::validate_ap rejects an incompatible callable against an async value IO")
{
    using string_fn_io =
        decltype(fnfelt::monad::io::create([] { return [](std::string) { return 1; }; }));
    using async_value_io = decltype(fnfelt::monad::io::create_async(async_identity, 41));

    // The callable's string parameter does not accept the async value IO's value, so validation
    // fails on the async path with the standard "does not accept" reason.
    static_assert(
        fnfelt::monad::io::ApIOTraits<>::validate_ap(^^string_fn_io, ^^async_value_io) ==
        "IO-wrapped function's callable does not accept the IO-wrapped value's value");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
