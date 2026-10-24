// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/create.hpp>

#include <doctest/doctest.h>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
/// Free state -> IO function, decayed to a function pointer by create.
auto free_state_action(int const & state)
{
    return fnfelt::monad::io::just(state + 1);
}

/// Copyable action carrying a payload, observing how it is stored.
struct PayloadAction
{
    int payload = 0;

    explicit PayloadAction(int payload_in) : payload{payload_in} {}
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(payload);
    }
};

/// Move-only action - must be moved, not copied, into the ReaderIO.
struct MoveOnlyPayloadAction  // NOLINT(*-special-member-functions)
{
    MoveOnlyPayloadAction() = default;
    MoveOnlyPayloadAction(MoveOnlyPayloadAction &&) = default;
    MoveOnlyPayloadAction & operator=(MoveOnlyPayloadAction &&) = delete;
    explicit MoveOnlyPayloadAction(int payload_in) : payload{payload_in} {}

    int payload;
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(payload);
    }
};

inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomReaderIOTraits =
    fnfelt::monad::reader_io::detail::CreateReaderIOTraits<custom_reader_io_name>;

/// Constant-evaluated pipeline via the named create overload.
consteval int constexpr_pipeline_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto reader = fnfelt::monad::reader_io::create<"X"_ss, int>(
        [](int const & state) { return fnfelt::monad::io::just(state + 1); });
    return reader.build(41).sync_wait();
}

/// Constant-evaluated pipeline via the default-traits create overload.
consteval int constexpr_pipeline_default()
{
    constexpr auto reader = fnfelt::monad::reader_io::create<int>(
        [](int const & state) { return fnfelt::monad::io::just(state + 2); });
    return reader.build(40).sync_wait();
}

/// Constant-evaluated pipeline via the explicit-traits create overload.
consteval int constexpr_pipeline_traits()
{
    constexpr auto reader = fnfelt::monad::reader_io::create<int, CustomReaderIOTraits>(
        [](int const & state) { return fnfelt::monad::io::just(state + 3); });
    return reader.build(39).sync_wait();
}
}  // namespace

TEST_CASE("create with a name wraps the action and names the ReaderIO")
{
    using namespace fnfelt::literals;  // NOLINT

    using action_t = decltype([](int const & state) { return fnfelt::monad::io::just(state); });
    auto reader = fnfelt::monad::reader_io::create<"MyReader"_ss, int>(action_t{});
    static_assert(std::is_same_v<decltype(reader)::action_type, action_t>);
    static_assert(std::is_same_v<decltype(reader)::state_type, int>);
    static_assert(std::is_same_v<decltype(reader)::value_type, int>);
    static_assert(decltype(reader)::traits::name == "MyReader");
    CHECK_EQ(reader.build(4).sync_wait(), 4);
}

TEST_CASE("create defaults to traits naming the ReaderIO \"ReaderIO\"")
{
    using action_t = decltype([](int const & state) { return fnfelt::monad::io::just(state); });
    auto reader = fnfelt::monad::reader_io::create<int>(action_t{});
    static_assert(decltype(reader)::traits::name == "ReaderIO");
    CHECK_EQ(reader.build(7).sync_wait(), 7);
}

TEST_CASE("create decays a function lvalue to a function pointer")
{
    auto reader = fnfelt::monad::reader_io::create<int>(&free_state_action);
    static_assert(std::is_same_v<decltype(reader)::action_type, decltype(&free_state_action)>);
    CHECK_EQ(reader.build(5).sync_wait(), 6);
}

TEST_CASE("create copies an lvalue action and moves an rvalue action")
{
    PayloadAction const lvalue_action{21};
    auto copied_reader = fnfelt::monad::reader_io::create<int>(lvalue_action);
    // The copy holds its own payload; the original is untouched.
    CHECK_EQ(lvalue_action.payload, 21);
    CHECK_EQ(copied_reader.build(0).sync_wait(), 21);

    auto moved_reader = fnfelt::monad::reader_io::create<int>(MoveOnlyPayloadAction{42});
    // A move-only action must be run through an rvalue ReaderIO.
    CHECK_EQ(std::move(moved_reader).build(0).sync_wait(), 42);
}

TEST_CASE("create accepts explicit traits for the ReaderIO")
{
    auto reader = fnfelt::monad::reader_io::create<int, CustomReaderIOTraits>(
        [](int const & /*state*/) { return fnfelt::monad::io::just(9); });
    static_assert(decltype(reader)::traits::name == "CustomReader");
    CHECK_EQ(reader.build(0).sync_wait(), 9);
}

TEST_CASE("create pipelines are constant-evaluable")
{
    static_assert(constexpr_pipeline_named() == 42);
    static_assert(constexpr_pipeline_default() == 42);
    static_assert(constexpr_pipeline_traits() == 42);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
