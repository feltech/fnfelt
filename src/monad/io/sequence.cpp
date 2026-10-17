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
namespace io = fnfelt::monad::io;

/// Move-only element action returning a value - must be moved, not copied, into the IO.
struct MoveOnlyElementAction
{
    std::unique_ptr<int> ptr{std::make_unique<int>(10)};

    MoveOnlyElementAction() = default;
    MoveOnlyElementAction(MoveOnlyElementAction &&) = default;
    MoveOnlyElementAction & operator=(MoveOnlyElementAction &&) = delete;
    int operator()() const
    {
        return *ptr;
    }
};

/// Element action that throws - the sequence must propagate the exception.
int throwing_element_action()
{
    throw std::runtime_error("sequence element failure");
}

/// Element action producing no value - the sequence has nothing to collect for it.
struct VoidElementAction
{
    void operator()() const {}
};

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

/// Stateless async element leaf that rendezvouses, then yields its value (poisoned on timeout).
inline constexpr auto async_rendezvous = [](auto,
                                            std::atomic<bool> * self_flag,
                                            std::atomic<bool> * peer_flag,
                                            int value) -> lf::task<int>
{ co_return rendezvous(self_flag, peer_flag) ? value : -1000; };

/// Stateless async element leaf yielding its argument unchanged.
inline constexpr auto async_identity = [](auto, int value) -> lf::task<int> { co_return value; };

/// Stateless async element leaf producing a pair, collected whole by a sequence.
inline constexpr auto async_pair = [](auto, int value) -> lf::task<std::pair<int, int>>
{ co_return std::pair{value, value + 1}; };

/// Stateless async element leaf that publishes its flag, then throws (never reaches its co_return).
inline constexpr auto async_throwing = [](auto, std::atomic<bool> * flag) -> lf::task<int>
{
    flag->store(true, std::memory_order_relaxed);
    throw std::runtime_error("async element failure");
    co_return 0;
};

/// Constant-evaluated IO pipeline sequencing a tuple of IOs.
consteval int constexpr_pipeline_sequence()
{
    constexpr auto collected =
        io::sequence(std::make_tuple(io::just(20), io::just(22))).build().sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

/// Constant-evaluated IO pipeline sequencing a tuple of IOs through the named overload.
consteval int constexpr_pipeline_sequence_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto collected =
        io::sequence<"Named sequence"_ss>(std::make_tuple(io::just(20), io::just(22)))
            .build()
            .sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = io::SequenceIOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline sequencing a tuple of IOs through the explicit-traits overload.
consteval int constexpr_pipeline_sequence_traits()
{
    constexpr auto collected =
        io::sequence<CustomIOTraits>(std::make_tuple(io::just(20), io::just(22)))
            .build()
            .sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

/// Custom traits delegating to the default traits, for the sequence injection points.
struct DelegatingSequenceTraits
{
    static constexpr std::string_view name = "DelegatingSequenceIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        return io::SequenceIOTraits<>::validate_sequence(tuple_meta);
    }
};
}  // namespace

namespace sequence_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using io::IO;
using io::SequenceIOTraits;

/// Element action of a named IO used in diagnostics.
struct ElementAction
{
    int operator()() const
    {
        return 1;
    }
};

using FirstIO = IO<ElementAction, SequenceIOTraits<"First"_ss>>;
using SecondIO = IO<ElementAction, SequenceIOTraits<"Second"_ss>>;

/// Tuple whose second element is an IO reference, for the reference diagnostics.
using RefSecondTuple = std::tuple<FirstIO, SecondIO &, int>;
}  // namespace sequence_error_msg_test

TEST_CASE("sequence collects the element values in tuple order")
{
    auto const sequenced =
        io::sequence(std::make_tuple(io::just(1), io::just(2.5), io::just(std::string{"three"})));
    static_assert(
        std::is_same_v<decltype(sequenced)::value_type, std::tuple<int, double, std::string>>);

    auto const collected = sequenced.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2.5);
    CHECK_EQ(std::get<2>(collected), "three");
}

TEST_CASE("sequence of an empty tuple yields an IO of an empty tuple")
{
    auto const sequenced = io::sequence(std::tuple{});
    static_assert(std::is_same_v<decltype(sequenced)::value_type, std::tuple<>>);
    static_assert(std::tuple_size_v<decltype(sequenced)::value_type> == 0);
    CHECK_EQ(std::tuple_size_v<std::remove_cvref_t<decltype(sequenced.build().sync_wait())>>, 0);
}

TEST_CASE("sequence of a single element yields a one-element tuple")
{
    auto const sequenced = io::sequence(std::make_tuple(io::just(7)));
    static_assert(std::is_same_v<decltype(sequenced)::value_type, std::tuple<int>>);
    CHECK_EQ(std::get<0>(sequenced.build().sync_wait()), 7);
}

TEST_CASE("sequence collects pair and tuple values whole rather than spread")
{
    auto const sequenced =
        io::sequence(std::make_tuple(io::just(std::pair{1, 2}), io::just(std::tuple{3, 4})));
    static_assert(std::is_same_v<
                  decltype(sequenced)::value_type,
                  std::tuple<std::pair<int, int>, std::tuple<int, int>>>);

    auto const collected = sequenced.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), (std::pair{1, 2}));
    CHECK_EQ(std::get<1>(std::get<1>(collected)), 4);
}

TEST_CASE("sequence is synchronous when every element is synchronous")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto const sequenced = io::sequence(std::make_tuple(io::just(1), io::just(2)));
    static_assert(!action_is_async(^^decltype(sequenced)::action_type));
    CHECK_EQ(std::get<1>(sequenced.build().sync_wait()), 2);
}

TEST_CASE("sequence is asynchronous when any element is asynchronous")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto const leading =
        io::sequence(std::make_tuple(io::create_async(async_identity, 1), io::just(2)));
    static_assert(action_is_async(^^decltype(leading)::action_type));
    auto const trailing =
        io::sequence(std::make_tuple(io::just(1), io::create_async(async_identity, 2)));
    static_assert(action_is_async(^^decltype(trailing)::action_type));

    CHECK_EQ(std::get<0>(leading.build().sync_wait(lf::lazy_pool{2})), 1);
    CHECK_EQ(std::get<1>(trailing.build().sync_wait(lf::lazy_pool{2})), 2);
}

TEST_CASE("sequence runs its async elements concurrently (atomic rendezvous)")
{
    // The flags live in this frame, which outlives sync_wait; the stateless leaves receive pointers
    // to them. Each element sets its own flag, then bounded-spins until it observes the other's. If
    // the elements were serialized on one thread, one spin would time out and its poison value
    // would flow through, failing the check.
    std::atomic<bool> first_flag{false};
    std::atomic<bool> second_flag{false};

    auto sequenced = io::sequence(
        std::make_tuple(
            io::create_async(async_rendezvous, &first_flag, &second_flag, 10),
            io::create_async(async_rendezvous, &second_flag, &first_flag, 20)));
    static_assert(fnfelt::monad::io::detail::action_is_async(^^decltype(sequenced)::action_type));

    auto const collected = sequenced.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected), 10);
    CHECK_EQ(std::get<1>(collected), 20);
}

TEST_CASE("sequence runs three async elements concurrently (atomic rendezvous)")
{
    // A ring: each element publishes its flag and waits for the next element's. A serialized run
    // times out a spin and yields poison values.
    std::atomic<bool> first_flag{false};
    std::atomic<bool> second_flag{false};
    std::atomic<bool> third_flag{false};

    auto sequenced = io::sequence(
        std::make_tuple(
            io::create_async(async_rendezvous, &first_flag, &second_flag, 10),
            io::create_async(async_rendezvous, &second_flag, &third_flag, 20),
            io::create_async(async_rendezvous, &third_flag, &first_flag, 30)));

    auto const collected = sequenced.build().sync_wait(lf::lazy_pool{4});
    CHECK_EQ(std::get<0>(collected), 10);
    CHECK_EQ(std::get<1>(collected), 20);
    CHECK_EQ(std::get<2>(collected), 30);
}

TEST_CASE("sequence overlaps a sync element with an async element (atomic rendezvous)")
{
    // The sync element is the value side of the fold step that composes it with the accumulated
    // fold, so it is forked and runs on another worker while the async element runs inline. Each
    // side sets its own flag, then bounded-spins until it observes the other's; a serialized run
    // times out a spin and yields poison values.
    std::atomic<bool> sync_flag{false};
    std::atomic<bool> async_flag{false};

    auto sequenced = io::sequence(
        std::make_tuple(
            io::create([&sync_flag, &async_flag]
                       { return rendezvous(&sync_flag, &async_flag) ? 10 : -1000; }),
            io::create_async(async_rendezvous, &async_flag, &sync_flag, 20)));

    auto const collected = sequenced.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected), 10);
    CHECK_EQ(std::get<1>(collected), 20);
}

TEST_CASE("sequence collects a pair produced by an async element whole")
{
    auto sequenced = io::sequence(std::make_tuple(io::create_async(async_pair, 3), io::just(9)));
    static_assert(
        std::is_same_v<decltype(sequenced)::value_type, std::tuple<std::pair<int, int>, int>>);

    auto const collected = sequenced.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected).first, 3);
    CHECK_EQ(std::get<0>(collected).second, 4);
    CHECK_EQ(std::get<1>(collected), 9);
}

TEST_CASE("sequence moves move-only element IOs")
{
    using fnfelt::monad::io::IO;

    auto sequenced = io::sequence(
        std::make_tuple(
            IO<MoveOnlyElementAction>{MoveOnlyElementAction{}},
            IO<MoveOnlyElementAction>{MoveOnlyElementAction{}}));
    static_assert(!std::is_copy_constructible_v<decltype(sequenced)>);

    auto const collected = std::move(sequenced).build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 10);
    CHECK_EQ(std::get<1>(collected), 10);
}

TEST_CASE("sequence accepts an lvalue tuple of IOs")
{
    auto ios = std::make_tuple(io::just(1), io::just(2));
    CHECK(io::sequence(ios).build().sync_wait() == std::tuple{1, 2});
    // The tuple is taken by value, so an lvalue is copied and its elements stay runnable.
    CHECK_EQ(std::get<0>(ios).build().sync_wait(), 1);
}

TEST_CASE("sequence accepts an async lvalue tuple of IOs")
{
    auto ios = std::make_tuple(io::create_async(async_identity, 1), io::just(2));
    auto const sequenced = io::sequence(ios);
    static_assert(fnfelt::monad::io::detail::action_is_async(^^decltype(sequenced)::action_type));

    auto const collected = sequenced.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2);
    CHECK_EQ(std::get<1>(ios).build().sync_wait(), 2);
}

TEST_CASE("sequence produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_sequence() == 42);
    static_assert(constexpr_pipeline_sequence_named() == 42);
    static_assert(constexpr_pipeline_sequence_traits() == 42);
}

TEST_CASE("sequence defaults to SequenceIOTraits for the result, not the element IOs' traits")
{
    using fnfelt::monad::io::IO;
    using SequenceIOTraits = io::SequenceIOTraits<>;

    auto const sequenced = io::sequence(
        std::make_tuple(
            IO<sequence_error_msg_test::ElementAction, CustomIOTraits>{
                sequence_error_msg_test::ElementAction{}},
            IO<sequence_error_msg_test::ElementAction, CustomIOTraits>{
                sequence_error_msg_test::ElementAction{}}));
    static_assert(std::is_same_v<decltype(sequenced)::traits, SequenceIOTraits>);
    CHECK_EQ(std::get<0>(sequenced.build().sync_wait()), 1);
}

TEST_CASE("sequence with a name names the result")
{
    using namespace fnfelt::literals;  // NOLINT

    auto const sequenced = io::sequence<"Named"_ss>(std::make_tuple(io::just(1), io::just(2)));
    static_assert(decltype(sequenced)::traits::name == "Named");
    CHECK_EQ(std::get<1>(sequenced.build().sync_wait()), 2);

    // A named sequence with an async element names the result on the async path too.
    auto const async = io::sequence<"Named async"_ss>(
        std::make_tuple(io::create_async(async_identity, 1), io::just(2)));
    static_assert(decltype(async)::traits::name == "Named async");
    CHECK_EQ(std::get<1>(async.build().sync_wait(lf::lazy_pool{2})), 2);
}

TEST_CASE("sequence consults custom traits that delegate to the default")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto const sequenced =
        io::sequence<DelegatingSequenceTraits>(std::make_tuple(io::just(1), io::just(2)));
    static_assert(std::is_same_v<decltype(sequenced)::traits, DelegatingSequenceTraits>);
    static_assert(!action_is_async(^^decltype(sequenced)::action_type));
    CHECK_EQ(std::get<1>(sequenced.build().sync_wait()), 2);

    auto async = io::sequence<DelegatingSequenceTraits>(
        std::make_tuple(io::create_async(async_identity, 1), io::just(2)));
    static_assert(std::is_same_v<decltype(async)::traits, DelegatingSequenceTraits>);
    static_assert(action_is_async(^^decltype(async)::action_type));
    CHECK_EQ(std::get<1>(async.build().sync_wait(lf::lazy_pool{2})), 2);
}

TEST_CASE("sequence composes with transform")
{
    auto const sequenced = io::sequence(std::make_tuple(io::just(1), io::just(2)));
    auto const summed = sequenced.transform([](std::tuple<int, int> values)
                                            { return std::get<0>(values) + std::get<1>(values); });
    CHECK_EQ(summed.build().sync_wait(), 3);
}

TEST_CASE("sequence propagates an exception thrown by a sync element")
{
    auto const sequenced =
        io::sequence(std::make_tuple(io::just(1), io::create(&throwing_element_action)));
    CHECK_THROWS_AS(static_cast<void>(sequenced.build().sync_wait()), std::runtime_error);
}

TEST_CASE("sequence propagates an exception thrown by an async element")
{
    std::atomic<bool> element_flag{false};

    auto sequenced =
        io::sequence(std::make_tuple(io::just(1), io::create_async(async_throwing, &element_flag)));
    static_assert(fnfelt::monad::io::detail::action_is_async(^^decltype(sequenced)::action_type));
    CHECK_THROWS_AS(
        static_cast<void>(sequenced.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
}

TEST_CASE("sequence propagates an async exception while a sync element is forked (rendezvous)")
{
    // The fold composes the sync element into the function side of the final `ap`, so it is forked
    // while the async element runs inline. The sync element spins only until the async element
    // publishes its flag, then returns, so the forked sibling has completed by the time the join
    // surfaces the async element's exception - the same shape as the async `ap` parity test.
    std::atomic<bool> async_flag{false};

    auto sequenced = io::sequence(
        std::make_tuple(
            io::create(
                [&async_flag]
                {
                    for (int spin = 0; spin < rendezvous_spin_limit; ++spin)
                    {
                        if (async_flag.load(std::memory_order_relaxed))
                        {
                            return 1;
                        }
                        if (spin % 1024 == 0)
                        {
                            std::this_thread::yield();
                        }
                    }
                    return -1000;
                }),
            io::create_async(async_throwing, &async_flag)));

    CHECK_THROWS_AS(
        static_cast<void>(sequenced.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
}

TEST_CASE("SequenceIOTraits::validate_sequence accepts IO tuples and rejects the rest")
{
    using SequenceIOTraits = io::SequenceIOTraits<>;

    // Every element an IO: accepted.
    static_assert(
        SequenceIOTraits::validate_sequence(
            ^^std::tuple<sequence_error_msg_test::FirstIO, sequence_error_msg_test::SecondIO>)
            .empty());
    // The empty tuple is accepted: it sequences to an IO of an empty tuple.
    static_assert(SequenceIOTraits::validate_sequence(^^std::tuple<>).empty());
    // A non-IO element names the standard reason.
    static_assert(
        SequenceIOTraits::validate_sequence(^^std::tuple<sequence_error_msg_test::FirstIO, int>) ==
        "element of the tuple is not an IO");
    // A reference element is rejected before the IO check, which sees through references.
    static_assert(
        SequenceIOTraits::validate_sequence(^^std::tuple<sequence_error_msg_test::FirstIO &>) ==
        "element of the tuple must not be a reference type");
    static_assert(
        SequenceIOTraits::validate_sequence(
            ^^std::tuple<sequence_error_msg_test::FirstIO, int const &>) ==
        "element of the tuple must not be a reference type");
    // An IO whose action produces no value has nothing to collect.
    static_assert(
        SequenceIOTraits::validate_sequence(
            ^^std::tuple<sequence_error_msg_test::FirstIO, io::IO<VoidElementAction>>) ==
        "element of the tuple produces no value");
    // A non-tuple argument names the tuple reason.
    static_assert(
        SequenceIOTraits::validate_sequence(^^int) == "provided tuple is not a std::tuple");
    // A reflection that is not a type names the non-type reason.
    static_assert(
        SequenceIOTraits::validate_sequence(^^io::detail::default_io_name) ==
        "provided tuple is not a type");
}

TEST_CASE("detail::is_sequence_tuple names tuple specialisations")
{
    using fnfelt::monad::io::detail::is_sequence_tuple;

    static_assert(is_sequence_tuple(^^std::tuple<int, double>));
    static_assert(is_sequence_tuple(^^std::tuple<>));
    // Qualifiers and references are stripped before inspecting the specialisation.
    static_assert(is_sequence_tuple(^^std::tuple<int> const &&));
    // Aliases are inspected through the type they name.
    static_assert(is_sequence_tuple(^^sequence_error_msg_test::RefSecondTuple));
    // The spread protocol's other tuple-like type is not a tuple.
    static_assert(!is_sequence_tuple(^^std::pair<int, int>));
    static_assert(!is_sequence_tuple(^^int));
    static_assert(!is_sequence_tuple(^^io::detail::default_io_name));
}

TEST_CASE("detail::sequence_error_msg composes a readable diagnostic")
{
    using fnfelt::monad::io::detail::sequence_error_msg;

    // All elements IOs: the element list shows the IO names, then the reason, then the offending
    // type. The tuple's template arguments are types rather than the aliases they were written
    // with, so the canonical display string is appended.
    static_assert(
        sequence_error_msg<
            ^^std::tuple<sequence_error_msg_test::FirstIO, sequence_error_msg_test::SecondIO>>(
            "SeqIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: SeqIO{(First, Second)}: custom reason: "} +
            std::string{display_string_of(dealias(^^sequence_error_msg_test::FirstIO))});

    // A non-IO element: shown as "<unknown>" in the element list, and it is the offending type.
    static_assert(
        sequence_error_msg<^^std::tuple<sequence_error_msg_test::FirstIO, int>>(
            "SeqIO", "custom reason") ==
        "fnfelt: IO sequence error: SeqIO{(First, <unknown>)}: custom reason: int");

    // A leading non-IO element is the offending type.
    static_assert(
        sequence_error_msg<^^std::tuple<int, sequence_error_msg_test::FirstIO>>(
            "SeqIO", "custom reason") ==
        "fnfelt: IO sequence error: SeqIO{(<unknown>, First)}: custom reason: int");

    // A reference element is the offending element, and is displayed through its referenced type: a
    // qualified reflection has no identifier and cannot be spliced.
    static_assert(
        sequence_error_msg<^^std::tuple<sequence_error_msg_test::FirstIO &, int>>(
            "SeqIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: SeqIO{(First, <unknown>)}: custom reason: "} +
            std::string{display_string_of(dealias(^^sequence_error_msg_test::FirstIO))});

    // A reference element is picked over the non-IO elements after it. The tuple is reflected
    // through an alias, exercising the diagnostic's dealiasing.
    static_assert(
        sequence_error_msg<^^sequence_error_msg_test::RefSecondTuple>("SeqIO", "custom reason") ==
        std::string{
            "fnfelt: IO sequence error: SeqIO{(First, Second, <unknown>)}: custom reason: "} +
            std::string{display_string_of(dealias(^^sequence_error_msg_test::SecondIO))});

    // The empty tuple has no elements to name: the tuple itself is the offending type.
    static_assert(
        sequence_error_msg<^^std::tuple<>>("SeqIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: SeqIO{()}: custom reason: "} +
            std::string{display_string_of(^^std::tuple<>)});

    // A non-tuple argument has no element list at all.
    static_assert(
        sequence_error_msg<^^int>("SeqIO", "custom reason") ==
        "fnfelt: IO sequence error: SeqIO{()}: custom reason: int");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
