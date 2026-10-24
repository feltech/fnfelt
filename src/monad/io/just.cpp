// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/just.hpp>

#include <doctest/doctest.h>

#include <meta>

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Copyable value that records how it has been transferred.
///
/// `copies` and `moves` count the copy and move constructions along the value's lineage (into
/// this object), so a test can tell whether a value reached it by copy or by move.
/// `is_moved_from` is a source-side flag, and the move constructor poisons the source's
/// `value`, so a test can tell whether a stored value was consumed between runs.
struct Observable
{
    int value{};
    int copies{};
    int moves{};
    bool is_moved_from{false};

    Observable() = default;
    explicit constexpr Observable(int initial) : value{initial} {}
    constexpr Observable(Observable const & other)
        : value{other.value}, copies{other.copies + 1}, moves{other.moves}
    {
    }
    constexpr Observable(Observable && other) noexcept
        : value{other.value}, copies{other.copies}, moves{other.moves + 1}
    {
        other.is_moved_from = true;
        other.value = -1;
    }
    Observable & operator=(Observable const &) = default;
    Observable & operator=(Observable &&) = delete;
};

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = fnfelt::monad::io::detail::JustIOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline via the named just overload.
consteval int constexpr_pipeline_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto io = fnfelt::monad::io::just<"X"_ss>(1);
    return io.build().sync_wait();
}

/// Constant-evaluated IO pipeline via the default-traits just overload.
consteval int constexpr_pipeline_default()
{
    constexpr auto io = fnfelt::monad::io::just(1);
    return io.build().sync_wait();
}

/// Constant-evaluated IO pipeline via the explicit-traits just overload.
consteval int constexpr_pipeline_traits()
{
    constexpr auto io = fnfelt::monad::io::just<CustomIOTraits>(1);
    return io.build().sync_wait();
}

/// Constant-evaluated rvalue run: the stored value is moved out of the IO.
consteval int constexpr_pipeline_rvalue()
{
    auto just_io = fnfelt::monad::io::just<CustomIOTraits>(Observable{11});
    auto const result = std::move(just_io).build().sync_wait();
    // One move lifts the value into the action, one moves the action into the IO, one moves the
    // action out of the IO, and one moves the value out of the run proxy; nothing is copied.
    if (result.copies == 0 && result.moves == 4)
    {
        return result.value;
    }
    return -999;
}

/// Custom traits that accept valid actions and delegate just validation to the default.
struct DelegatingJustTraits : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "DelegatingJustIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_just(std::meta::info value_meta)
    {
        return fnfelt::monad::io::detail::JustIOTraits<>::validate_just(value_meta);
    }
};

/// Custom traits that accept everything, proving the validator is consulted.
struct RelaxedTraits : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "RelaxedIO";

    // `validate_action` must also accept everything: IO's class-scope validation probes
    // const-receiver invocability, which is a hard error (the deleted copy constructor) when the
    // stored value is move-only.
    static consteval std::string_view validate_action(std::meta::info /*action_meta*/)
    {
        return {};
    }

    static consteval std::string_view validate_just(std::meta::info /*value_meta*/)
    {
        return {};
    }
};
}  // namespace

TEST_CASE("just with a name lifts the value and names the IO")
{
    using namespace fnfelt::literals;  // NOLINT

    auto io = fnfelt::monad::io::just<"MyIO"_ss>(1);
    static_assert(std::is_same_v<decltype(io)::value_type, int>);
    static_assert(decltype(io)::traits::name == "MyIO");
    CHECK_EQ(io.build().sync_wait(), 1);
}

TEST_CASE("just defaults to traits naming the IO \"IO\"")
{
    auto io = fnfelt::monad::io::just(2);
    static_assert(std::is_same_v<decltype(io)::traits, fnfelt::monad::io::detail::JustIOTraits<>>);
    static_assert(decltype(io)::traits::name == "IO");
    CHECK_EQ(io.build().sync_wait(), 2);
}

TEST_CASE("just with explicit JustIOTraits names the IO via the traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto io = fnfelt::monad::io::just<fnfelt::monad::io::detail::JustIOTraits<"Explicit"_ss>>(3);
    static_assert(decltype(io)::traits::name == "Explicit");
    CHECK_EQ(io.build().sync_wait(), 3);
}

TEST_CASE("just with custom traits uses those traits")
{
    auto io = fnfelt::monad::io::just<CustomIOTraits>(4);
    static_assert(std::is_same_v<decltype(io)::traits, CustomIOTraits>);
    CHECK_EQ(io.build().sync_wait(), 4);
}

TEST_CASE("just decays and copies lvalues, leaving the source usable")
{
    using namespace fnfelt::literals;  // NOLINT

    auto source = Observable{5};
    auto io = fnfelt::monad::io::just<"L"_ss>(source);
    static_assert(std::is_same_v<decltype(io)::value_type, Observable>);
    // The lvalue is copied into the action and the source is untouched.
    CHECK_FALSE(source.is_moved_from);
    CHECK_EQ(source.value, 5);
    // The value is copied into the action, moved into the IO, copied out of the IO into the run
    // proxy, and moved into the result: two copies and two moves.
    auto result = io.build().sync_wait();
    CHECK_EQ(result.value, 5);
    CHECK_EQ(result.copies, 2);
    CHECK_EQ(result.moves, 2);
    CHECK_FALSE(result.is_moved_from);
    CHECK_FALSE(source.is_moved_from);
    CHECK_EQ(source.value, 5);
}

TEST_CASE("just moves rvalues into the stored value, which lvalue runs copy out intact")
{
    using namespace fnfelt::literals;  // NOLINT

    auto source = Observable{7};
    auto io = fnfelt::monad::io::just<"M"_ss>(std::move(source));
    // The rvalue is moved into the action and the IO constructor moves the action into its
    // member: two moves and no copies.
    CHECK(source.is_moved_from);
    // Each lvalue run then copies the stored value out of the IO and moves it into the result, so
    // each result carries two input moves plus one copy and one move per run.
    auto first = io.build().sync_wait();
    CHECK_EQ(first.value, 7);
    CHECK_EQ(first.copies, 1);
    CHECK_EQ(first.moves, 3);
    CHECK_FALSE(first.is_moved_from);
    // The second run still sees the intact stored value: it would be poisoned if the first
    // run had consumed it.
    auto second = io.build().sync_wait();
    CHECK_EQ(second.value, 7);
    CHECK_EQ(second.copies, 1);
    CHECK_EQ(second.moves, 3);
}

TEST_CASE("const lvalue runs copy the stored value out, leaving the IO runnable")
{
    using namespace fnfelt::literals;  // NOLINT

    auto io = fnfelt::monad::io::just<"C"_ss>(Observable{9});
    auto const & const_io = io;
    // The rvalue is moved into the action and the IO constructor moves the action into its
    // member: two moves and no copies. A const lvalue run copies the stored value out, exactly
    // like a mutable lvalue run, giving one copy and three moves per result.
    auto first = const_io.build().sync_wait();
    CHECK_EQ(first.value, 9);
    CHECK_EQ(first.copies, 1);
    CHECK_EQ(first.moves, 3);
    auto second = const_io.build().sync_wait();
    CHECK_EQ(second.value, 9);
    CHECK_EQ(second.copies, 1);
    CHECK_EQ(second.moves, 3);
}

TEST_CASE("rvalue runs move the stored value out, consuming the IO")
{
    using namespace fnfelt::literals;  // NOLINT

    auto just_io = fnfelt::monad::io::just<"Rvalue"_ss>(Observable{9});
    // The rvalue is moved into the action, the IO constructor moves the action into its member,
    // the rvalue run moves the action out of the IO into the proxy, and the stored value is moved
    // out of the proxy into the result: four moves and no copies.
    auto first = std::move(just_io).build().sync_wait();
    CHECK_EQ(first.value, 9);
    CHECK_EQ(first.copies, 0);
    CHECK_EQ(first.moves, 4);
    CHECK_FALSE(first.is_moved_from);
    // The consumed IO's stored value was poisoned by the move, so the lvalue run copies that
    // moved-from value out of the IO and moves it into the result: one copy on top of the two
    // construction moves.
    auto second = just_io.build().sync_wait();
    CHECK_EQ(second.value, -1);
    CHECK_EQ(second.copies, 1);
    CHECK_EQ(second.moves, 3);
}

TEST_CASE("just produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_named() == 1);
    static_assert(constexpr_pipeline_default() == 1);
    static_assert(constexpr_pipeline_traits() == 1);
    static_assert(constexpr_pipeline_rvalue() == 11);
}

TEST_CASE("just with delegating traits validates through the traits and runs")
{
    auto io = fnfelt::monad::io::just<DelegatingJustTraits>(5);
    static_assert(std::is_same_v<decltype(io)::traits, DelegatingJustTraits>);
    static_assert(decltype(io)::traits::name == "DelegatingJustIO");
    CHECK_EQ(io.build().sync_wait(), 5);
}

TEST_CASE("just with relaxed custom traits accepts a move-only value")
{
    // The default JustIOTraits::validate_just rejects a move-only value; custom traits whose
    // validate_just accepts everything lets it through, proving the validator is consulted.
    auto just_io = fnfelt::monad::io::just<RelaxedTraits>(std::make_unique<int>(13));
    static_assert(std::is_same_v<decltype(just_io)::value_type, std::unique_ptr<int>>);
    // A move-only value can only be consumed by an rvalue run; a const/lvalue run would copy it.
    auto const result = std::move(just_io).build().sync_wait();
    CHECK_EQ(*result, 13);
}

namespace just_error_msg_test
{
/// Stable named value type; its short name and display string are deterministic.
struct Value
{
};

/// Alias for `Value`; reflection must dealias it so the diagnostic still names `Value`.
using ValueAlias = Value;
}  // namespace just_error_msg_test

TEST_CASE("detail::just_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::just_error_msg;

    static_assert(
        just_error_msg<^^just_error_msg_test::Value>("JustIO", "rejected by custom just traits") ==
        "fnfelt: IO just error: JustIO{() => Value}: rejected by custom just traits: "
        "just_error_msg_test::Value");
    static_assert(
        just_error_msg<^^just_error_msg_test::ValueAlias>(
            "JustIO", "rejected by custom just traits") ==
        "fnfelt: IO just error: JustIO{() => Value}: rejected by custom just traits: "
        "just_error_msg_test::Value");
}

TEST_CASE("named IO just can store a ReaderIO value with both umbrellas included")
{
    using namespace fnfelt::literals;  // NOLINT
    auto source = fnfelt::monad::reader_io::just<int>(42);
    auto plain_value = fnfelt::monad::io::just<"reader value"_ss>(source).build().sync_wait();
    CHECK_EQ(plain_value.build(1).sync_wait(), 42);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
