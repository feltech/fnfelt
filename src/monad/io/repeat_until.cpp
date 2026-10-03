// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/select.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/static_string.hpp>

TEST_CASE("repeat_until constant evaluates nested parallel and selective IO")
{
    namespace io = fnfelt::monad::io;
    constexpr auto pipeline = []
    {
        auto choice =
            io::sequence(io::just(20), io::just(22))
                .transform([](int lhs, int rhs)
                           { return std::expected<int, int>{std::unexpected{lhs + rhs}}; });
        auto fallback = io::ap(
            io::just([](int offset) { return [offset](int value) { return value + offset; }; }),
            io::just(0));
        return io::select(choice, fallback)
            .repeat_until(
                0,
                [](int accumulated, int value)
                { return std::expected<int, int>{accumulated + value}; });
    };
    static_assert(pipeline().build().sync_wait() == 42);
    CHECK_EQ(pipeline().build().sync_wait(lf::lazy_pool{2}), 42);
}

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
using fnfelt::monad::io::detail::RepeatUntilIOTraits;

/// Source action counting how often it is run; every iteration's value is constant.
struct RepeatCounterAction
{
    /// Number of runs observed since the last reset.
    static inline int calls = 0;

    int operator()() const
    {
        ++calls;
        return 1;
    }
};

/// Source action whose payload would become empty if the action were moved out.
struct PayloadAction
{
    std::string payload = "payload";

    std::string operator()() const
    {
        return payload;
    }
};

/// Stepper accumulating into a member: only an lvalue receiver keeps the sum across iterations.
struct AccumulatingStepper
{
    int sum = 0;

    std::expected<int, int> operator()(int state, int value)
    {
        sum += value;
        if (state == 2)
        {
            return std::expected<int, int>{sum};
        }
        return std::unexpected{state + 1};
    }
};

/// Constant-evaluated IO pipeline looping with the member repeat_until and a value-type change.
consteval int constexpr_pipeline_repeat_until()
{
    constexpr auto repeated =
        fnfelt::monad::io::create([] { return 1; })
            .repeat_until(
                0,
                [](int state, int value) -> std::expected<std::string, int>
                {
                    if (state == 2)
                    {
                        // std::to_string is not constexpr on libstdc++ 16.2.
                        return std::expected<std::string, int>{
                            std::string(1, static_cast<char>('0' + value)) + "!"};
                    }
                    return std::unexpected{state + 1};
                });
    return repeated.build().sync_wait().size();
}

/// Constant-evaluated IO pipeline looping with the named free repeat_until.
consteval int constexpr_pipeline_repeat_until_free_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto repeated = fnfelt::monad::io::repeat_until<"Named free repeat_until"_ss>(
        fnfelt::monad::io::create([] { return 1; }),
        0,
        [](int state, int value) -> std::expected<int, int>
        {
            if (state == 2)
            {
                return std::expected<int, int>{value + state * 10};
            }
            return std::unexpected{state + 1};
        });
    return repeated.build().sync_wait();
}

/// Stateless coroutine entry producing its bound argument on every run.
inline constexpr auto async_one = [](auto, int value) -> lf::task<int> { co_return value; };
}  // namespace

TEST_CASE("IO repeat_until re-runs the source IO on every iteration and stops at success")
{
    using fnfelt::monad::io::IO;

    RepeatCounterAction::calls = 0;
    auto repeated = fnfelt::monad::io::repeat_until(
        IO<RepeatCounterAction, fnfelt::monad::io::detail::RepeatUntilIOTraits<>>{
            RepeatCounterAction{}},
        0,
        [](int state, int value) -> std::expected<int, int>
        {
            if (state == 2)
            {
                return std::expected<int, int>{value + state * 10};
            }
            return std::unexpected{state + 1};
        });

    // Iterations: state 0 -> 1 -> 2 -> success, so the source runs three times and the loop yields
    // the third iteration's value.
    CHECK_EQ(repeated.build().sync_wait(), 21);
    CHECK_EQ(RepeatCounterAction::calls, 3);

    // A re-run starts from the initial state again (the action, with its state, is copied by
    // build), re-running the source for the same number of iterations.
    CHECK_EQ(repeated.build().sync_wait(), 21);
    CHECK_EQ(RepeatCounterAction::calls, 6);
}

TEST_CASE("IO repeat_until keeps the source intact across iterations")
{
    using fnfelt::monad::io::IO;

    // The source action holds a std::string that would be moved out if the source were moved into
    // the run proxy: every iteration would then see an empty payload, so the loop must be copying
    // the source action per iteration.
    auto repeated = fnfelt::monad::io::repeat_until(
        IO<PayloadAction, fnfelt::monad::io::detail::RepeatUntilIOTraits<>>{PayloadAction{}},
        0,
        [](int state, std::string value) -> std::expected<std::string, int>
        {
            if (value.empty())
            {
                return std::expected<std::string, int>{"moved-from"};
            }
            if (state == 2)
            {
                return std::expected<std::string, int>{value};
            }
            return std::unexpected{state + 1};
        });

    CHECK_EQ(repeated.build().sync_wait(), "payload");

    // An lvalue source is copied into the composition, so the caller's IO stays usable.
    auto const source = fnfelt::monad::io::create([] { return 7; });
    auto bound = fnfelt::monad::io::repeat_until(
        source,
        0,
        [](int state, int value) -> std::expected<int, int>
        {
            if (state == 0)
            {
                return std::expected<int, int>{value};
            }
            return std::unexpected{state + 1};
        });
    CHECK_EQ(bound.build().sync_wait(), 7);
    CHECK_EQ(source.build().sync_wait(), 7);
}

TEST_CASE("IO repeat_until threads state through std::unexpected and changes the value type")
{
    using fnfelt::monad::io::IO;

    auto prev_io = fnfelt::monad::io::create([] { return 1; });
    auto new_io = fnfelt::monad::io::repeat_until(
        prev_io,
        0,
        [](int state, int value) -> std::expected<std::string, int>
        {
            if (state == 2)
            {
                return std::expected<std::string, int>{std::to_string(value) + "!"};
            }
            return std::unexpected{state + 1};
        });

    // The new IO's value type is the stepper's expected value; the source IO's is unchanged.
    static_assert(std::is_same_v<decltype(prev_io)::value_type, int>);
    static_assert(std::is_same_v<decltype(new_io)::value_type, std::string>);
    static_assert(
        std::is_same_v<decltype(new_io)::traits, fnfelt::monad::io::detail::RepeatUntilIOTraits<>>);

    CHECK_EQ(new_io.build().sync_wait(), "1!");
}

TEST_CASE("IO repeat_until threads a move-only state as the unexpected value")
{
    using fnfelt::monad::io::IO;

    auto repeated = fnfelt::monad::io::repeat_until(
        fnfelt::monad::io::create([] { return 1; }),
        std::make_unique<int>(0),
        [](std::unique_ptr<int> state, int value) -> std::expected<int, std::unique_ptr<int>>
        {
            if (*state >= 2)
            {
                return std::expected<int, std::unique_ptr<int>>{value + *state};
            }
            return std::unexpected{std::make_unique<int>(*state + value)};
        });

    // Iterations: 0 -> 1 -> 2 -> 2 + 1 produces 3. The move-only state makes the action move-only,
    // so the IO is consumed as an rvalue, as in the other operations' move-only tests.
    static_assert(!std::is_copy_constructible_v<decltype(repeated)>);
    CHECK_EQ(std::move(repeated).build().sync_wait(), 3);
}

TEST_CASE("IO repeat_until invokes the stepper as an lvalue receiver")
{
    // The stepper accumulates into a member across iterations, which requires an lvalue receiver.
    auto repeated = fnfelt::monad::io::repeat_until(
        fnfelt::monad::io::create([] { return 1; }), 0, AccumulatingStepper{});

    // Three iterations of value 1 accumulate to 3, returned when state reaches 2.
    CHECK_EQ(repeated.build().sync_wait(), 3);
}

TEST_CASE("IO repeat_until accepts the free, named-free, member and named-member forms")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    auto const stepper = [](int state, int value) -> std::expected<int, int>
    {
        if (state == 2)
        {
            return std::expected<int, int>{value + state * 10};
        }
        return std::unexpected{state + 1};
    };

    // Free function with default traits.
    auto free_default =
        fnfelt::monad::io::repeat_until(fnfelt::monad::io::create([] { return 1; }), 0, stepper);
    static_assert(std::is_same_v<
                  decltype(free_default)::traits,
                  fnfelt::monad::io::detail::RepeatUntilIOTraits<>>);
    CHECK_EQ(free_default.build().sync_wait(), 21);

    // Free function with a name.
    auto free_named = fnfelt::monad::io::repeat_until<"Named free repeat_until"_ss>(
        fnfelt::monad::io::create([] { return 1; }), 0, stepper);
    static_assert(decltype(free_named)::traits::name == "Named free repeat_until");
    CHECK_EQ(free_named.build().sync_wait(), 21);

    // Member with default traits.
    auto member_default = fnfelt::monad::io::create([] { return 1; }).repeat_until(0, stepper);
    static_assert(std::is_same_v<
                  decltype(member_default)::traits,
                  fnfelt::monad::io::detail::RepeatUntilIOTraits<>>);
    CHECK_EQ(member_default.build().sync_wait(), 21);

    // Member with a name.
    auto member_named = fnfelt::monad::io::create([] { return 1; })
                            .template repeat_until<"Named member repeat_until"_ss>(0, stepper);
    static_assert(decltype(member_named)::traits::name == "Named member repeat_until");
    CHECK_EQ(member_named.build().sync_wait(), 21);
}

TEST_CASE("IO repeat_until produces IOs usable in a constexpr pipeline")
{
    // The helper returns the final result string's size: the loop's value is "1!" (two characters),
    // since the stepper appends an exclamation mark to the produced value.
    static_assert(constexpr_pipeline_repeat_until() == 2);
    static_assert(constexpr_pipeline_repeat_until_free_named() == 21);
}

TEST_CASE("IO repeat_until runs an async source until the stepper reports success")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto repeated = fnfelt::monad::io::repeat_until(
        fnfelt::monad::io::create_async(async_one, 7),
        0,
        [](int state, int value) -> std::expected<int, int>
        {
            if (state == 1)
            {
                return std::expected<int, int>{value + 3};
            }
            return std::unexpected{state + 1};
        });

    // The async source makes the whole composition asynchronous.
    static_assert(action_is_async(^^decltype(repeated)::action_type));
    // Two iterations: state 0 -> 1 -> success with the second iteration's value 7 + 3.
    CHECK_EQ(repeated.build().sync_wait(), 10);

    // An explicit scheduler services the whole loop the same way.
    CHECK_EQ(repeated.build().sync_wait(lf::lazy_pool{2}), 10);
}

namespace repeat_until_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::IO;
using fnfelt::monad::io::detail::RepeatUntilIOTraits;

using SourceIO = IO<int (*)(), fnfelt::monad::io::detail::RepeatUntilIOTraits<"SourceIO"_ss>>;

/// Stepper returning an expected with an int value; its display string is stable.
struct ExpectedStepper
{
    std::expected<int, int> operator()(int, int) const;
};

/// Alias for the expected stepper, used to check aliases do not leak into the diagnostic.
using StepperAlias = ExpectedStepper;

/// Stepper returning a plain int, so no expected value can be named.
struct IntReturningStepper
{
    int operator()(int, int) const;
};

/// Free-function stepper whose pointer-to-function type has no short name.
std::expected<int, int> pointer_stepper(int, int);

/// Lambda stepper whose closure type renders as "lambda".
inline constexpr auto lambda_stepper = [](int, int) { return std::expected<int, int>{1}; };

/// Incomplete state type; its display string is stable.
struct Incomplete;

/// Valid source IO for the async diagnostic case.
inline constexpr auto async_diag_source = fnfelt::monad::io::create_async<"AsyncSource"_ss>(
    [](auto, int x) -> lf::task<int> { co_return x; }, 21);
}  // namespace repeat_until_error_msg_test

TEST_CASE("detail::repeat_until_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::repeat_until_error_msg;

    // Valid source IO and stepper: the stepper is named with its loop signature (state and
    // source value names in the parameter list, expected's value type after the arrow), then the
    // reason, and the offending type is the stepper.
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^int,
            ^^repeat_until_error_msg_test::ExpectedStepper>(
            "RepeatUntilIO", "rejected by custom repeat traits") ==
        "fnfelt: IO repeat_until error: RepeatUntilIO{SourceIO, ExpectedStepper(int, int) => "
        "int}: rejected by custom repeat traits: repeat_until_error_msg_test::ExpectedStepper");

    // A caller-side alias for a valid stepper: the reflections are dealiased before naming, so the
    // signature and the offender name the underlying stepper, never the alias.
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^int,
            ^^repeat_until_error_msg_test::StepperAlias>("IO", "custom reason") ==
        "fnfelt: IO repeat_until error: IO{SourceIO, ExpectedStepper(int, int) => int}: custom "
        "reason: repeat_until_error_msg_test::ExpectedStepper");

    // Non-IO source: neither the source name nor the source value nor the result is determinable,
    // and the offending type is the source.
    static_assert(
        repeat_until_error_msg<^^int, ^^int, ^^repeat_until_error_msg_test::ExpectedStepper>(
            "RepeatUntilIO", "custom reason") ==
        "fnfelt: IO repeat_until error: RepeatUntilIO{<unknown>, ExpectedStepper(int, <unknown>) "
        "=> <unknown>}: custom reason: int");

    // Source IO with a stepper whose result is not an expected: the result is "<unknown>" and the
    // offender is the stepper.
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^int,
            ^^repeat_until_error_msg_test::IntReturningStepper>(
            "IO", "stepper function must return a std::expected") ==
        "fnfelt: IO repeat_until error: IO{SourceIO, IntReturningStepper(int, int) => <unknown>}: "
        "stepper function must return a std::expected: "
        "repeat_until_error_msg_test::IntReturningStepper");

    // Async source: the diagnostic gets the source's name the same way as on the synchronous
    // path, through maybe_io_name - here the async leaf's traits name ("AsyncSource").
    static_assert(
        repeat_until_error_msg<
            ^^decltype(repeat_until_error_msg_test::async_diag_source),
            ^^int,
            ^^repeat_until_error_msg_test::IntReturningStepper>(
            "IO", "stepper function must return a std::expected") ==
        "fnfelt: IO repeat_until error: IO{AsyncSource, IntReturningStepper(int, int) => "
        "<unknown>}: stepper function must return a std::expected: "
        "repeat_until_error_msg_test::IntReturningStepper");

    // An incomplete state type: the offender is the state, which still shows its identifier
    // because it has a short name, and the result stays "<unknown>".
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^repeat_until_error_msg_test::Incomplete,
            ^^repeat_until_error_msg_test::ExpectedStepper>("IO", "state is not a complete type") ==
        "fnfelt: IO repeat_until error: IO{SourceIO, ExpectedStepper(Incomplete, int) => "
        "<unknown>}: state is not a complete type: repeat_until_error_msg_test::Incomplete");

    // A function-pointer stepper has no short name: it renders as "<unknown>" while its loop
    // signature and the expected's value stay determinable.
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^int,
            ^^decltype(&repeat_until_error_msg_test::pointer_stepper)>("IO", "custom reason") ==
        "fnfelt: IO repeat_until error: IO{SourceIO, <unknown>(int, int) => int}: custom reason: "
        "std::expected<int, int> (*)(int, int)");

    // A lambda stepper is a closure type, whose short name is "lambda". The offender's display
    // string is the closure's compiler-specific rendering, so the expectation is composed with it
    // (as in the sequence_error_msg tests).
    static_assert(
        repeat_until_error_msg<
            ^^repeat_until_error_msg_test::SourceIO,
            ^^int,
            ^^decltype(repeat_until_error_msg_test::lambda_stepper)>("IO", "custom reason") ==
        std::string{"fnfelt: IO repeat_until error: IO{SourceIO, lambda(int, int) => int}: custom "
                    "reason: "} +
            std::string{
                display_string_of(^^decltype(repeat_until_error_msg_test::lambda_stepper))});
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
