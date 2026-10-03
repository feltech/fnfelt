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

#include <immer/array.hpp>

#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io.hpp>
#include <fnfelt/static_string.hpp>

// Magic numbers are used in tests.
// Unnamed parameters are used in test stubs.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
namespace io = fnfelt::monad::io;

/// Element action that throws - the traversal must propagate the exception at run time.
int throwing_element_action()
{
    throw std::runtime_error("traverse element failure");
}

/// Element action producing no value - the IO it wraps produces nothing to run onwards.
struct VoidElementAction
{
    void operator()() const {}
};

/// Copyable action recording lazy execution and repeated runs.
struct CountingAction
{
    std::atomic<int> * calls;
    int value;

    int operator()() const
    {
        ++*calls;
        return value;
    }
};

/// Synchronous leaf yielding its stored value.
struct ConstantAction
{
    int value;

    int operator()() const
    {
        return value;
    }
};

/// Continuation recording how many times it is applied, mapping each value to a counting IO.
struct CountingContinuation
{
    int * applied;
    std::atomic<int> * runs;

    auto operator()(int value) const
    {
        ++*applied;
        return io::create(CountingAction{runs, value});
    }
};

/// Continuation that throws when applied - an empty tuple never applies it.
struct NeverApplyContinuation
{
    auto operator()(int value) const
    {
        throw std::runtime_error("traverse of an empty tuple must not apply the continuation");
        return io::just(value);
    }
};

/// Stateless async leaf yielding its argument unchanged.
inline constexpr auto async_identity = [](auto, int value) -> lf::task<int> { co_return value; };

/// Continuation doubling a value into a synchronous IO.
inline constexpr auto double_to_io = [](int value) { return io::just(value * 2); };

/// IO type a `just` of an int produces - spells the free-function continuation's return type.
using JustIntIO = std::remove_cvref_t<decltype(io::just(0))>;

/**
 * Free function continuation yielding a `just` IO of its argument plus five.
 *
 * The IO return type is spelled so the continuation is a plain function: passed by name, the
 * by-value parameter deduces it to a function pointer.
 */
JustIntIO int_to_just_io(int value)
{
    return io::just(value + 5);
}

/// Continuation returning a plain value, not an IO - rejected by validation.
inline constexpr auto non_io_continuation = [](int value) { return value; };

/// Continuation yielding its value into an asynchronous IO.
inline constexpr auto async_to_io = [](int value)
{ return io::create_async(async_identity, value); };

/// Continuation spreading a pair value into a synchronous IO of their sum.
inline constexpr auto spread_pair_to_io = [](int first, int second)
{ return io::just(first + second); };

/// Continuation overloaded for a pair value and for two ints - direct invocation wins over
/// spreading, so a pair element takes the pair overload, not the spread two-int one.
struct PairOrSpreadContinuation
{
    auto operator()(std::pair<int, int> value) const
    {
        return io::just(value.first * 100 + value.second);
    }

    auto operator()(int first, int second) const
    {
        return io::just(first + second);
    }
};

/// Continuation that throws for one value - the throw surfaces from the traverse call itself.
inline constexpr auto throwing_continuation = [](int value)
{
    if (value == 2)
    {
        throw std::runtime_error("traverse continuation failure");
    }
    return io::just(value);
};

/// Continuation returning an IO that produces no value - rejected by validation.
inline constexpr auto void_io_continuation = []([[maybe_unused]] int value)
{ return io::IO<VoidElementAction>{VoidElementAction{}}; };

/// Continuation accepting only a string value - an int element's value is not accepted.
inline constexpr auto string_continuation = [](std::string) { return io::just(1); };

/// Continuation callable only as an rvalue - the lvalue invocation must be rejected.
struct RvalueOnlyContinuation
{
    auto operator()(int value) &&
    {
        return io::just(value);
    }
};

/// Reference to an rvalue-only continuation - shape validation rejects reference continuations
/// before invocation is considered.
using RvalueOnlyContinuationRef = RvalueOnlyContinuation &&;

/// Incomplete continuation type - reflection can name it, but invocation cannot be asked about.
struct IncompleteContinuation;

/// Move-only continuation adding a pointer-held offset to each value - accepted as an rvalue.
struct MoveOnlyContinuation
{
    std::unique_ptr<int> offset{std::make_unique<int>(2)};

    MoveOnlyContinuation() = default;
    MoveOnlyContinuation(MoveOnlyContinuation &&) = default;
    MoveOnlyContinuation & operator=(MoveOnlyContinuation &&) = delete;

    auto operator()(int value) const
    {
        return io::just(value + *offset);
    }
};

/// Continuation with a non-const call operator that counts its applications through a member
/// pointer - mutable continuations are accepted because the lvalue invocation is non-const.
struct MutableContinuation
{
    int * calls;

    auto operator()(int value)
    {
        ++*calls;
        return io::just(value);
    }
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

/// Continuation whose per-value synchronous leaves rendezvous with each other.
struct RendezvousContinuation
{
    /// Flags published by the per-value leaves, keyed on the element's value.
    std::atomic<bool> * first_flag;
    std::atomic<bool> * second_flag;

    auto operator()(int value) const
    {
        auto * self_flag = value == 10 ? first_flag : second_flag;
        auto * peer_flag = value == 10 ? second_flag : first_flag;
        return io::create([self_flag, peer_flag, value]
                          { return rendezvous(self_flag, peer_flag) ? value : -1000; });
    }
};

/// Constant-evaluated IO pipeline traversing a tuple of values.
consteval int constexpr_pipeline_traverse()
{
    constexpr auto collected =
        io::traverse(std::make_tuple(20, 22), double_to_io).build().sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

/// Constant-evaluated IO pipeline traversing a tuple of values through the named overload.
consteval int constexpr_pipeline_traverse_named()
{
    using namespace fnfelt::literals;  // NOLINT
    constexpr auto collected =
        io::traverse<"Named traverse"_ss>(std::make_tuple(20, 22), double_to_io)
            .build()
            .sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

inline constexpr char custom_io_name[] = "CustomIO";
using CustomIOTraits = io::detail::TraverseIOTraits<custom_io_name>;

/// Constant-evaluated IO pipeline traversing a tuple of values through the explicit-traits
/// overload.
consteval int constexpr_pipeline_traverse_traits()
{
    constexpr auto collected =
        io::traverse<CustomIOTraits>(std::make_tuple(20, 22), double_to_io).build().sync_wait();
    return std::get<0>(collected) + std::get<1>(collected);
}

/// Custom traits delegating to the default traits, for the traverse injection points.
struct DelegatingTraverseTraits : io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "DelegatingTraverseIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        return io::detail::SequenceIOTraits<>::validate_sequence(tuple_meta);
    }

    static consteval std::string_view validate_traverse(
        std::meta::info tuple_meta, std::meta::info continuation_meta)
    {
        return io::detail::TraverseIOTraits<>::validate_traverse(tuple_meta, continuation_meta);
    }
};
}  // namespace

namespace traverse_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using io::IO;
using io::detail::TraverseIOTraits;

/// Element action of a named IO used in diagnostics.
struct ElementAction
{
    int operator()() const
    {
        return 1;
    }
};

using FirstIO = IO<ElementAction, fnfelt::monad::io::detail::TraverseIOTraits<"First"_ss>>;
using SecondIO = IO<ElementAction, fnfelt::monad::io::detail::TraverseIOTraits<"Second"_ss>>;

/// Value the continuation rejects - the offender named in diagnostics.
struct SecondValue
{
};

/// Continuation returning a named IO, used in the result-name and offending diagnostics. A named
/// struct so its display string is stable across translation units.
struct NamedContinuation
{
    auto operator()(int value) const
    {
        return io::just<"Result"_ss>(value);
    }
};

/// Continuation returning each IO value unchanged, used in the IO-value validation and the
/// offending diagnostics. A named struct so its display string is stable across translation units.
struct NamedIdentityContinuation
{
    auto operator()(auto value) const
    {
        return value;
    }
};
}  // namespace traverse_error_msg_test

TEST_CASE(
    "traverse applies the continuation to each element value and collects the results in order")
{
    auto const traversed = io::traverse(
        std::make_tuple(1, 2.5, std::string{"three"}), [](auto value) { return io::just(value); });
    static_assert(
        std::is_same_v<decltype(traversed)::value_type, std::tuple<int, double, std::string>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2.5);
    CHECK_EQ(std::get<2>(collected), "three");
}

TEST_CASE("traverse runs the IO the continuation returns for each value")
{
    auto const traversed = io::traverse(std::make_tuple(10, 20, 30), double_to_io);
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 20);
    CHECK_EQ(std::get<1>(collected), 40);
    CHECK_EQ(std::get<2>(collected), 60);
}

TEST_CASE("traverse applies the continuation once per element when the composition is built")
{
    int applied = 0;
    std::atomic<int> runs{0};
    auto const traversed =
        io::traverse(std::make_tuple(1, 2, 3), CountingContinuation{&applied, &runs});
    // The continuation is applied eagerly to each value when the composition is built, but the
    // mapped IOs only run when the result runs.
    CHECK_EQ(applied, 3);
    CHECK_EQ(runs.load(), 0);

    auto const collected = traversed.build().sync_wait();
    CHECK(collected == std::tuple{1, 2, 3});
    CHECK_EQ(applied, 3);
    CHECK_EQ(runs.load(), 3);

    // A second run re-runs the mapped IOs without applying the continuation again.
    CHECK_EQ(traversed.build().sync_wait(), collected);
    CHECK_EQ(applied, 3);
    CHECK_EQ(runs.load(), 6);
}

TEST_CASE("traverse of an empty tuple yields an IO of an empty tuple and never applies it")
{
    // The empty tuple never applies the continuation, so a throwing continuation (and a move-only
    // one) is still accepted and yields an IO of an empty tuple.
    auto const traversed = io::traverse(std::tuple{}, NeverApplyContinuation{});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<>>);
    CHECK_EQ(std::tuple_size_v<std::remove_cvref_t<decltype(traversed.build().sync_wait())>>, 0);

    auto const move_only_traversed = io::traverse(std::tuple{}, MoveOnlyContinuation{});
    static_assert(std::is_same_v<decltype(move_only_traversed)::value_type, std::tuple<>>);
    CHECK_EQ(
        std::tuple_size_v<std::remove_cvref_t<decltype(move_only_traversed.build().sync_wait())>>,
        0);
}

TEST_CASE("traverse of a single element yields a one-element tuple")
{
    auto const traversed = io::traverse(std::make_tuple(7), double_to_io);
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int>>);
    CHECK_EQ(std::get<0>(traversed.build().sync_wait()), 14);
}

TEST_CASE("traverse spreads a pair value into the continuation")
{
    auto const traversed =
        io::traverse(std::make_tuple(std::pair{1, 2}, std::pair{3, 4}), spread_pair_to_io);
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 3);
    CHECK_EQ(std::get<1>(collected), 7);
}

TEST_CASE("traverse prefers direct invocation over spreading")
{
    // Both the pair overload and the two-int overload accept a pair element, but direct invocation
    // wins over spreading, so the pair overload maps it (102, not the spread sum 3).
    auto const traversed =
        io::traverse(std::make_tuple(std::pair{1, 2}), PairOrSpreadContinuation{});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int>>);

    CHECK_EQ(std::get<0>(traversed.build().sync_wait()), 102);
}

TEST_CASE("traverse uses runtime concurrency when every continuation result is synchronous")
{
    auto const traversed = io::traverse(std::make_tuple(1, 2), double_to_io);
    static_assert(io::detail::action_is_async(^^decltype(traversed)::action_type));
    CHECK_EQ(std::get<1>(traversed.build().sync_wait()), 4);
}

TEST_CASE("traverse is asynchronous when the continuation returns an asynchronous IO")
{
    auto const traversed = io::traverse(std::make_tuple(1, 2), async_to_io);
    static_assert(io::detail::action_is_async(^^decltype(traversed)::action_type));

    auto const collected = traversed.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2);
}

TEST_CASE("traverse runs its synchronous mapped IOs concurrently (atomic rendezvous)")
{
    // The continuation maps the two values to leaves that rendezvous with each other, so the mapped
    // IOs must overlap. The flags live in this frame, which outlives sync_wait. If the mapped IOs
    // were serialized on one thread, one spin would time out and its poison value would flow
    // through, failing the check.
    std::atomic<bool> first_flag{false};
    std::atomic<bool> second_flag{false};

    auto const traversed =
        io::traverse(std::make_tuple(10, 20), RendezvousContinuation{&first_flag, &second_flag});
    static_assert(io::detail::action_is_async(^^decltype(traversed)::action_type));

    auto const collected = traversed.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(collected), 10);
    CHECK_EQ(std::get<1>(collected), 20);
}

TEST_CASE("traverse moves move-only element values into the continuation")
{
    // The tuple is taken by value and moved into the composition, so a move-only element value is
    // moved, not copied, into the continuation's parameter.
    auto const traversed = io::traverse(
        std::make_tuple(std::make_unique<int>(1), std::make_unique<int>(2)),
        [](std::unique_ptr<int> value) { return io::just(*value); });
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2);
}

TEST_CASE("traverse accepts a move-only continuation")
{
    // The continuation is taken by value and invoked as an lvalue, never copied per element, so a
    // move-only continuation is accepted for a non-empty tuple.
    auto const traversed = io::traverse(std::make_tuple(1, 2), MoveOnlyContinuation{});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 3);
    CHECK_EQ(std::get<1>(collected), 4);
}

TEST_CASE("traverse accepts a mutable continuation")
{
    // A non-const call operator is accepted: the continuation is held by value in the call and
    // invoked as a non-const lvalue, so it may mutate its own state (order-independent).
    int applied = 0;
    auto const traversed = io::traverse(std::make_tuple(1, 2), MutableContinuation{&applied});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);
    CHECK_EQ(applied, 2);

    auto const collected = traversed.build().sync_wait();
    CHECK(collected == std::tuple{1, 2});
    // Running the result applies no per-element continuation again.
    CHECK_EQ(applied, 2);
}

TEST_CASE("traverse accepts an lvalue tuple of values")
{
    auto values = std::make_tuple(1, 2);
    CHECK(io::traverse(values, double_to_io).build().sync_wait() == std::tuple{2, 4});
    // The tuple is taken by value, so an lvalue is copied and the caller's tuple stays usable.
    CHECK_EQ(std::get<0>(values), 1);
}

TEST_CASE("traverse accepts a named and custom-traits traversal")
{
    using namespace fnfelt::literals;  // NOLINT

    auto const named = io::traverse<"Named"_ss>(std::make_tuple(1, 2), double_to_io);
    static_assert(decltype(named)::traits::name == "Named");
    CHECK_EQ(std::get<1>(named.build().sync_wait()), 4);

    auto const custom = io::traverse<DelegatingTraverseTraits>(std::make_tuple(1, 2), double_to_io);
    static_assert(std::is_same_v<decltype(custom)::traits, DelegatingTraverseTraits>);
    CHECK_EQ(std::get<1>(custom.build().sync_wait()), 4);
}

TEST_CASE("traverse with the identity continuation collects a tuple of IOs like sequence")
{
    auto const inputs = std::make_tuple(io::just(1), io::just(2));
    // Each IO is a value, and the continuation returns it unchanged, so the traversal composes the
    // same mapped IOs a sequence of the same tuple composes.
    auto const traversed = io::traverse(inputs, [](auto value) { return value; });
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    CHECK_EQ(traversed.build().sync_wait(), io::sequence(inputs).build().sync_wait());
}

TEST_CASE("traverse composes with transform")
{
    auto const traversed = io::traverse(std::make_tuple(1, 2), double_to_io);
    auto const summed = traversed.transform([](std::tuple<int, int> values)
                                            { return std::get<0>(values) + std::get<1>(values); });
    CHECK_EQ(summed.build().sync_wait(), 6);
}

TEST_CASE("traverse propagates an exception thrown by the continuation")
{
    // The continuation runs eagerly when the composition is built, so the throw surfaces from the
    // traverse call itself, before any mapped IO could run.
    CHECK_THROWS_AS(
        static_cast<void>(io::traverse(std::make_tuple(1, 2), throwing_continuation)),
        std::runtime_error);
}

TEST_CASE("traverse propagates an exception thrown by a continuation's IO")
{
    auto const traversed = io::traverse(
        std::make_tuple(1, 2),
        []([[maybe_unused]] int value) { return io::create(&throwing_element_action); });
    CHECK_THROWS_AS(static_cast<void>(traversed.build().sync_wait()), std::runtime_error);
}

TEST_CASE("traverse invokes the continuation as an lvalue")
{
    // Ref-qualified continuation: the & overload is the one validation, inference and the driver
    // must all select, since the continuation is held by value in the call and invoked as an
    // lvalue. The && overload would double into 2.5, but lvalue invocation picks the 1.
    struct RefQualifiedContinuation
    {
        auto operator()([[maybe_unused]] int value) &
        {
            return io::just(1);
        }

        auto operator()([[maybe_unused]] int value) &&
        {
            return io::just(2.5);
        }
    };

    auto const traversed = io::traverse(std::make_tuple(1, 2), RefQualifiedContinuation{});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 1);
}

TEST_CASE("traverse accepts a continuation returning a reference to an IO")
{
    // The continuation returns a reference to its IO member. Validation accepts an IO reference
    // (the mapped IO is copied into the sequence), and the diagnostic is instantiated even for a
    // valid call, so naming the reference result IO must not hard-error on the splice.
    struct ReferenceReturningContinuation
    {
        io::IO<ConstantAction> element_io{ConstantAction{7}};

        io::IO<ConstantAction> & operator()([[maybe_unused]] int value) &
        {
            return element_io;
        }
    };

    static_assert(io::detail::TraverseIOTraits<>::validate_traverse(
                      ^^std::tuple<int, int>, ^^ReferenceReturningContinuation)
                      .empty());

    auto const traversed = io::traverse(std::make_tuple(1, 2), ReferenceReturningContinuation{});
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    // The single member IO is copied into each element slot, so both elements yield its value.
    CHECK_EQ(std::get<0>(collected), 7);
    CHECK_EQ(std::get<1>(collected), 7);
}

TEST_CASE("traverse accepts a function pointer continuation")
{
    // The continuation is passed by name, so the by-value parameter deduces a function pointer.
    auto const traversed = io::traverse(std::make_tuple(10, 20), int_to_just_io);
    static_assert(std::is_same_v<decltype(traversed)::value_type, std::tuple<int, int>>);

    auto const collected = traversed.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 15);
    CHECK_EQ(std::get<1>(collected), 25);
}

TEST_CASE("traverse produces IOs usable in a constexpr pipeline")
{
    static_assert(constexpr_pipeline_traverse() == 84);
    static_assert(constexpr_pipeline_traverse_named() == 84);
    static_assert(constexpr_pipeline_traverse_traits() == 84);
}

TEST_CASE("TraverseIOTraits::validate_traverse accepts valid tuples and rejects the rest")
{
    using Traits = io::detail::TraverseIOTraits<>;

    // A valid tuple of values with a continuation returning an IO: accepted.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int, double>, ^^decltype(double_to_io)).empty());
    // The empty tuple is accepted: the continuation is never applied.
    static_assert(Traits::validate_traverse(^^std::tuple<>, ^^decltype(double_to_io)).empty());
    // A non-tuple type names the tuple reason.
    static_assert(
        Traits::validate_traverse(^^int, ^^decltype(double_to_io)) ==
        "provided tuple is not a std::tuple");
    // A non-type reflection names the "not a type" reason.
    static_assert(
        Traits::validate_traverse(^^throwing_element_action, ^^decltype(double_to_io)) ==
        "provided tuple is not a type");
    // A continuation that is not a type is rejected before its shape is considered.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^throwing_element_action) ==
        "continuation function is not a type");
    // An incomplete continuation type is rejected before invocation is considered: invocation
    // queries on an incomplete type are hard, non-catchable errors.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^IncompleteContinuation) ==
        "continuation function is not a complete type");
    // A reference-typed continuation is rejected after completeness is established.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^RvalueOnlyContinuationRef) ==
        "continuation function must not be a reference type");
    // A complete type that is neither a class nor a function pointer type is a rejected shape.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^int) ==
        "continuation function is not a class or function pointer type");
    // A continuation that accepts none of the values is rejected on the first element whose value
    // it does not accept.
    static_assert(
        Traits::validate_traverse(
            ^^std::tuple<int, std::string>, ^^decltype(string_continuation)) ==
        "continuation function does not accept the tuple element's value");
    // A continuation callable only as an rvalue rejects the non-const lvalue invocation the
    // composition models.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^RvalueOnlyContinuation) ==
        "continuation function does not accept the tuple element's value");
    // A continuation returning a plain value names the IO reason.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^decltype(non_io_continuation)) ==
        "continuation function must return an IO");
    // A continuation returning an IO that produces no value leaves nothing to feed onwards.
    static_assert(
        Traits::validate_traverse(^^std::tuple<int>, ^^decltype(void_io_continuation)) ==
        "continuation function must return an IO that produces a value");
    // A move-only continuation is accepted: it is taken by value and invoked as an lvalue, never
    // copied per element.
    static_assert(Traits::validate_traverse(^^std::tuple<int>, ^^MoveOnlyContinuation).empty());
    // A tuple of IOs is a tuple of values: the continuation is applied to the IO object itself, so
    // a value-mapping continuation does not accept an IO element's value.
    static_assert(
        Traits::validate_traverse(
            ^^std::tuple<traverse_error_msg_test::FirstIO, traverse_error_msg_test::SecondIO>,
            ^^decltype(double_to_io)) ==
        "continuation function does not accept the tuple element's value");
    // A continuation returning each IO unchanged maps IO values to IOs: accepted, recovering a
    // sequence of the tuple.
    static_assert(
        Traits::validate_traverse(
            ^^std::tuple<traverse_error_msg_test::FirstIO, traverse_error_msg_test::SecondIO>,
            ^^traverse_error_msg_test::NamedIdentityContinuation)
            .empty());
    // The sequence traits also accept an immer::array, but traverse composes a tuple sequence.
    static_assert(
        Traits::validate_traverse(
            ^^immer::array<traverse_error_msg_test::FirstIO>, ^^decltype(double_to_io)) ==
        "provided tuple is not a std::tuple");
}

TEST_CASE("detail::traverse_error_msg composes a readable diagnostic")
{
    using io::detail::traverse_error_msg;

    // Value elements are named by their types, and a continuation returning a named IO names the
    // result IO. The element list shows the value names, the continuation result shows the result
    // IO's name, then the reason, then the offending type (the continuation, since every element is
    // accepted).
    static_assert(
        traverse_error_msg<^^std::tuple<int>, ^^traverse_error_msg_test::NamedContinuation>(
            "TravIO", "custom reason") ==
        "fnfelt: IO traverse error: TravIO{(int) => Result}: custom reason: " +
            std::string{display_string_of(dealias(^^traverse_error_msg_test::NamedContinuation))});

    // A heterogeneous tuple whose continuation accepts the first value but not the second: the
    // element list names the second element by its short name, and the second element itself is
    // the offending argument, named at the end through its display string (which qualifies the
    // namespace, so it is pinned through the same reflection helper).
    static_assert(
        traverse_error_msg<
            ^^std::tuple<int, traverse_error_msg_test::SecondValue>,
            ^^decltype(double_to_io)>("TravIO", "custom reason") ==
        std::string{"fnfelt: IO traverse error: TravIO{(int, "} +
            std::string{fnfelt::detail::short_type_name(^^traverse_error_msg_test::SecondValue)} +
            ") => IO}: custom reason: " +
            std::string{display_string_of(dealias(^^traverse_error_msg_test::SecondValue))});

    // A tuple of IOs names its elements through their traits, and with every IO accepted (the
    // continuation returns each IO unchanged) the continuation itself is the offending argument.
    static_assert(
        traverse_error_msg<
            ^^std::tuple<traverse_error_msg_test::FirstIO, traverse_error_msg_test::SecondIO>,
            ^^traverse_error_msg_test::NamedIdentityContinuation>("TravIO", "custom reason") ==
        std::string{
            "fnfelt: IO traverse error: TravIO{(First, Second) => First}: custom reason: "} +
            std::string{
                display_string_of(dealias(^^traverse_error_msg_test::NamedIdentityContinuation))});

    // The empty tuple has no elements to name or result to infer: the continuation is the offending
    // argument.
    static_assert(
        traverse_error_msg<^^std::tuple<>, ^^traverse_error_msg_test::NamedContinuation>(
            "TravIO", "custom reason") ==
        std::string{"fnfelt: IO traverse error: TravIO{() => <unknown>}: custom reason: "} +
            std::string{display_string_of(dealias(^^traverse_error_msg_test::NamedContinuation))});

    // A tuple whose continuation fails its own shape checks - int is neither a class nor a function
    // pointer type - names the continuation as the offending argument, though the element list is
    // still emitted (an element scan would blame the SecondValue element instead). The element's
    // name differs from the continuation's, so the pin discriminates which one was selected.
    static_assert(
        traverse_error_msg<^^std::tuple<traverse_error_msg_test::SecondValue>, ^^int>(
            "TravIO", "custom reason") ==
        std::string{"fnfelt: IO traverse error: TravIO{("} +
            std::string{fnfelt::detail::short_type_name(^^traverse_error_msg_test::SecondValue)} +
            ") => <unknown>}: custom reason: int");

    // A non-tuple input has no element list or result to name, and it is the offending argument.
    static_assert(
        traverse_error_msg<^^int, ^^decltype(double_to_io)>("TravIO", "custom reason") ==
        "fnfelt: IO traverse error: TravIO{<unknown> => <unknown>}: custom reason: int");
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
