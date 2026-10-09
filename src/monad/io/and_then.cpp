// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <memory>
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
/// Move-only continuation that reads through a unique_ptr as a const lvalue - passes validation.
struct MoveOnlyContinuation
{
    std::unique_ptr<int> ptr{std::make_unique<int>(2)};

    auto operator()(int value) const
    {
        return fnfelt::monad::io::IO{[this, value] { return value + *ptr; }};
    }
};

/// Move-only action incrementing a move counter on every move - pins that rvalue chains move.
struct MoveTrackingAction
{
    /// Number of move constructions observed since the last reset.
    static inline int moves = 0;
    std::unique_ptr<int> ptr{std::make_unique<int>(1)};

    MoveTrackingAction() = default;
    MoveTrackingAction(MoveTrackingAction && other) noexcept : ptr{std::move(other.ptr)}
    {
        ++moves;
    }
    MoveTrackingAction & operator=(MoveTrackingAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::IOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline chaining two and_then steps.
consteval int constexpr_pipeline_and_then()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 10; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline chaining via the named and_then overload.
consteval int constexpr_pipeline_and_then_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .template and_then<"Named and_then"_ss>(
                [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline chaining via the explicit-traits and_then overload.
consteval int constexpr_pipeline_and_then_traits()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return 1; })
            .template and_then<CustomIOTraits>(
                [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline chaining via the free and_then function.
consteval int constexpr_pipeline_and_then_free()
{
    constexpr auto bound = fnfelt::monad::io::and_then(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline chaining via the free named and_then function.
consteval int constexpr_pipeline_and_then_free_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto bound = fnfelt::monad::io::and_then<"Named free and_then"_ss>(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline chaining via the free explicit-traits and_then function.
consteval int constexpr_pipeline_and_then_free_traits()
{
    constexpr auto bound = fnfelt::monad::io::and_then<CustomIOTraits>(
        fnfelt::monad::io::create([] { return 1; }),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    return bound().sync_wait();
}

/// Constant-evaluated IO pipeline spreading a pair value into the continuation.
consteval int constexpr_pipeline_spread()
{
    constexpr auto bound =
        fnfelt::monad::io::create([] { return std::pair{1, 2}; })
            .and_then([](int x, int y)
                      { return fnfelt::monad::io::create([x, y] { return x + y; }); });
    return bound().sync_wait();
}
}  // namespace

TEST_CASE("IO and_then accepts a direct continuation and runs source then continuation")
{
    using fnfelt::monad::io::IO;

    auto const bound =
        IO{[] { return 1; }}.and_then([](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(bound().sync_wait(), 2);
}

TEST_CASE("IO and_then spreads pair and tuple values into the continuation")
{
    using fnfelt::monad::io::IO;

    auto const bound_pair = IO{[] { return std::pair{1, 2}; }}.and_then(
        [](int x, int y) { return IO{[x, y] { return x + y; }}; });
    CHECK_EQ(bound_pair().sync_wait(), 3);

    auto const bound_tuple = IO{[] { return std::tuple{3, 4}; }}.and_then(
        [](int x, int y) { return IO{[x, y] { return x * y; }}; });
    CHECK_EQ(bound_tuple().sync_wait(), 12);
}

TEST_CASE("IO and_then prefers direct invocation over spreading")
{
    using fnfelt::monad::io::IO;

    // The continuation accepts the whole pair, so it must be invoked directly even though the value
    // could also be spread.
    auto const bound = IO{[] { return std::pair{1, 2}; }}.and_then(
        [](std::pair<int, int> pair_value)
        { return IO{[pair_value] { return pair_value.first * 10 + pair_value.second; }}; });
    CHECK_EQ(bound().sync_wait(), 12);
}

TEST_CASE("IO and_then chains through successive continuations")
{
    using fnfelt::monad::io::IO;

    auto const bound = IO{[] { return 1; }}
                           .and_then([](int x) { return IO{[x] { return x + 1; }}; })
                           .and_then([](int x) { return IO{[x] { return x * 10; }}; });
    CHECK_EQ(bound().sync_wait(), 20);
}

TEST_CASE("IO and_then accepts an lvalue continuation and copies it")
{
    using fnfelt::monad::io::IO;

    // A named copyable continuation must be accepted (copied by value), not deduced as a reference
    // type and rejected.
    struct IdentityContinuation
    {
        int offset;

        auto operator()(int value) const
        {
            return IO{[value, offset = offset] { return value + offset; }};
        }
    };

    IdentityContinuation continuation_lvalue{10};
    auto const bound = IO{[] { return 1; }}.and_then(continuation_lvalue);
    CHECK_EQ(bound().sync_wait(), 11);

    // The lvalue is untouched, so it remains usable.
    CHECK_EQ(continuation_lvalue(2)().sync_wait(), 12);
}

TEST_CASE("IO and_then works on a const IO and with a move-only continuation")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 5; }};
    auto bound = source.and_then(MoveOnlyContinuation{});
    CHECK_EQ(std::move(bound)().sync_wait(), 7);
}

TEST_CASE("IO with custom traits chains and runs end-to-end")
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
    auto const bound = source.and_then(
        [](int x) { return IO<CustomAction, CustomIOTraits>{CustomAction{x + 1}}; });
    CHECK_EQ(bound().sync_wait(), 2);
}

TEST_CASE("and_then member defaults to IOTraits for the result")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;

    // Source IO with custom traits: the result takes the default IOTraits, not the source's.
    IO const source{[] { return 1; }};
    auto const bound = source.and_then([](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    CHECK_EQ(bound().sync_wait(), 2);
}

TEST_CASE("and_then member with explicit traits uses those traits for the result")
{
    using fnfelt::monad::io::IO;

    IO const source{[] { return 1; }};
    auto const bound =
        source.template and_then<CustomIOTraits>([](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(std::is_same_v<decltype(bound)::traits, CustomIOTraits>);
    CHECK_EQ(bound().sync_wait(), 2);
}

TEST_CASE("and_then member with a name names the result")
{
    using fnfelt::monad::io::IO;
    using namespace fnfelt::literals;  // NOLINT

    IO const source{[] { return 1; }};
    auto const bound = source.template and_then<"Named and_then"_ss>(
        [](int x) { return IO{[x] { return x + 1; }}; });
    static_assert(decltype(bound)::traits::name == "Named and_then");
    CHECK_EQ(bound().sync_wait(), 2);
}

TEST_CASE("free and_then uses the default, named and explicit-traits forms")
{
    using fnfelt::monad::io::IO;
    using IOTraits = fnfelt::monad::io::IOTraits<>;
    using namespace fnfelt::literals;  // NOLINT

    auto const source = fnfelt::monad::io::create<CustomIOTraits>([] { return 1; });
    auto const bound = fnfelt::monad::io::and_then(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(std::is_same_v<decltype(bound)::traits, IOTraits>);
    CHECK_EQ(bound().sync_wait(), 2);

    auto const named = fnfelt::monad::io::and_then<"Named free and_then"_ss>(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(decltype(named)::traits::name == "Named free and_then");
    CHECK_EQ(named().sync_wait(), 2);

    auto const trait_bound = fnfelt::monad::io::and_then<CustomIOTraits>(
        source, [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(std::is_same_v<decltype(trait_bound)::traits, CustomIOTraits>);
    CHECK_EQ(trait_bound().sync_wait(), 2);
}

TEST_CASE("and_then produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_and_then() == 20);
    static_assert(constexpr_pipeline_and_then_named() == 2);
    static_assert(constexpr_pipeline_and_then_traits() == 2);
    static_assert(constexpr_pipeline_and_then_free() == 2);
    static_assert(constexpr_pipeline_and_then_free_named() == 2);
    static_assert(constexpr_pipeline_and_then_free_traits() == 2);
    static_assert(constexpr_pipeline_spread() == 3);
}

TEST_CASE("and_then moves a move-only rvalue source through a chain without copying")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    // A move-only source cannot be copied, so it must be moved into the AndThenAction member. The
    // action is move-only, hence so is the resulting IO.
    MoveTrackingAction::moves = 0;
    auto bound = IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}}.and_then(
        [](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(std::move(bound)().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    // Five moves. Construction: the temporary action into the source IO's action member; the source
    // IO into the AndThenAction's source member; and the AndThenAction into the returned IO's
    // action member (three). Running: the IO's action into the RunProxy by value; and the nested
    // source IO into its own RunProxy when the AndThenAction runs (two). The rvalue temporary is
    // elided into the IO constructor's by-value parameter, so it costs no extra move.
    CHECK_EQ(MoveTrackingAction::moves, 5);

    // Each further hop moves the whole nested source IO once into the new AndThenAction member and
    // the new AndThenAction once into the returned IO's action member, so the two-hop chain costs
    // two more moves than the one-hop chain to construct. Running adds one more nested source-IO
    // move for the extra hop, so eight in total.
    MoveTrackingAction::moves = 0;
    auto chained = IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}}
                       .and_then([](int x) { return IO{[x] { return x + 1; }}; })
                       .and_then([](int x) { return IO{[x] { return x * 10; }}; });
    CHECK_EQ(std::move(chained)().sync_wait(), 20);
    static_assert(!std::is_copy_constructible_v<decltype(chained)>);
    CHECK_EQ(MoveTrackingAction::moves, 8);

    // The free function is a forwarding reference too, so a directly-passed prvalue rvalue source
    // costs the same as the member form (the temporary is direct-initialised into the stored
    // action). Pinned here so a regression to a by-value source parameter would show up for the
    // move-only case.
    MoveTrackingAction::moves = 0;
    auto free_bound = fnfelt::monad::io::and_then(
        IO<MoveTrackingAction, IOTraits<>>{MoveTrackingAction{}},
        [](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(std::move(free_bound)().sync_wait(), 2);
    static_assert(!std::is_copy_constructible_v<decltype(free_bound)>);
    CHECK_EQ(MoveTrackingAction::moves, 5);
}

TEST_CASE("and_then copies an lvalue IO source, leaving the caller's IO usable")
{
    using fnfelt::monad::io::IO;

    // A copyable action so the lvalue source can be copied; the caller's IO must be untouched.
    auto const source = IO{[] { return 1; }};
    auto const bound =
        fnfelt::monad::io::and_then(source, [](int x) { return IO{[x] { return x + 1; }}; });
    CHECK_EQ(bound().sync_wait(), 2);
    CHECK_EQ(source().sync_wait(), 1);
}

namespace and_then_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using fnfelt::monad::io::IO;
using fnfelt::monad::io::IOTraits;

using SourceIO = IO<int (*)(), IOTraits<"SourceIO"_ss>>;
using ResultIO = IO<int (*)(), IOTraits<"ResultIO"_ss>>;

/// Continuation returning a custom-named IO; its display string is stable.
struct CustomResultContinuation
{
    ResultIO operator()(int) const;
};

/// Continuation that is not callable with the source value, so no result IO is determinable.
struct UncallableContinuation
{
};
}  // namespace and_then_error_msg_test

TEST_CASE("detail::and_then_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::and_then_error_msg;

    // Valid source IO and custom-named result: both IO names are shown, then the reason, and the
    // offending type is the continuation.
    static_assert(
        and_then_error_msg<
            ^^and_then_error_msg_test::SourceIO,
            ^^and_then_error_msg_test::CustomResultContinuation>(
            "AndThenIO", "rejected by custom continuation traits") ==
        "fnfelt: IO and_then error: AndThenIO{(SourceIO()) => ResultIO}: rejected by custom "
        "continuation traits: and_then_error_msg_test::CustomResultContinuation");

    // Non-IO source: neither IO name is determinable, and the offending type is the source.
    static_assert(
        and_then_error_msg<^^int, ^^and_then_error_msg_test::CustomResultContinuation>(
            "AndThenIO", "custom reason") ==
        "fnfelt: IO and_then error: AndThenIO{(<unknown>()) => <unknown>}: custom reason: int");

    // Source IO whose continuation result cannot be determined: the result name is "<unknown>".
    static_assert(
        and_then_error_msg<
            ^^and_then_error_msg_test::SourceIO,
            ^^and_then_error_msg_test::UncallableContinuation>("AndThenIO", "custom reason") ==
        "fnfelt: IO and_then error: AndThenIO{(SourceIO()) => <unknown>}: custom reason: "
        "and_then_error_msg_test::UncallableContinuation");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
