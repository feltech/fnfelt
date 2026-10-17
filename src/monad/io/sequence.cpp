// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <atomic>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include <immer/array.hpp>
#include <immer/memory_policy.hpp>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/async.hpp>
#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/static_string.hpp>

#include "detail/launch_failure.hpp"

TEST_CASE("sequence joins children when launching a later child throws")
{
    namespace failure = launch_failure_test;
    failure::State state;
    auto first = fnfelt::monad::io::create(failure::Action<int>{&state, true});
    auto second = fnfelt::monad::io::create(failure::Action<int>{&state, false});
    SUBCASE("tuple")
    {
        auto collected = fnfelt::monad::io::sequence(std::make_tuple(first, second));
        CHECK_THROWS_WITH_AS(
            static_cast<void>(std::move(collected).build().sync_wait(lf::lazy_pool{2})),
            "child launch failure",
            std::runtime_error);
    }
    SUBCASE("array")
    {
        auto collected = fnfelt::monad::io::sequence(immer::array{first, second});
        CHECK_THROWS_WITH_AS(
            static_cast<void>(std::move(collected).build().sync_wait(lf::lazy_pool{2})),
            "child launch failure",
            std::runtime_error);
    }
    CHECK(state.finished.load(std::memory_order_acquire));
    CHECK_FALSE(state.timed_out.load(std::memory_order_relaxed));
}

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

/// Copyable array value that cannot be default-constructed.
struct ArrayValue
{
    int value;

    explicit ArrayValue(int value_in) : value{value_in} {}
};

/// Copyable array element action recording lazy execution and repeated runs.
struct ArrayElementAction
{
    std::atomic<int> * calls;
    int value;

    int operator()() const
    {
        ++*calls;
        return value;
    }
};

/// Copyable array action producing a fresh move-only value on each run.
struct ArrayMoveOnlyValueAction
{
    int value;

    auto operator()() const
    {
        return std::make_unique<int>(value);
    }
};

/// Copy-counted action that consumes its independently owned value when run.
struct ArrayConsumingAction
{
    std::unique_ptr<int> value;
    std::atomic<int> * copies;

    ArrayConsumingAction(int value_in, std::atomic<int> * copies_in)
        : value{std::make_unique<int>(value_in)}, copies{copies_in}
    {
    }

    ArrayConsumingAction(ArrayConsumingAction const & other)
        : value{std::make_unique<int>(*other.value)}, copies{other.copies}
    {
        ++*copies;
    }

    ArrayConsumingAction(ArrayConsumingAction &&) = default;

    int operator()() &&
    {
        return *std::exchange(value, nullptr);
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
using CustomIOTraits = io::detail::SequenceIOTraits<custom_io_name>;

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
struct DelegatingSequenceTraits : io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "DelegatingSequenceIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        return io::detail::SequenceIOTraits<>::validate_sequence(tuple_meta);
    }
};
}  // namespace

namespace sequence_error_msg_test
{
using namespace fnfelt::literals;  // NOLINT

using io::IO;
using io::detail::SequenceIOTraits;

/// Element action of a named IO used in diagnostics.
struct ElementAction
{
    int operator()() const
    {
        return 1;
    }
};

using FirstIO = IO<ElementAction, fnfelt::monad::io::detail::SequenceIOTraits<"First"_ss>>;
using SecondIO = IO<ElementAction, fnfelt::monad::io::detail::SequenceIOTraits<"Second"_ss>>;

/// Tuple whose second element is an IO reference, for the reference diagnostics.
using RefSecondTuple = std::tuple<FirstIO, SecondIO &, int>;
}  // namespace sequence_error_msg_test

TEST_CASE("sequence collects an immutable array lazily and preserves its order")
{
    std::atomic<int> calls{0};
    auto const element = [&calls](int value)
    { return io::create(ArrayElementAction{&calls, value}); };
    auto const ios = immer::array{element(3), element(1), element(2)};
    auto const sequenced = io::sequence(ios);
    static_assert(std::is_same_v<decltype(sequenced)::value_type, immer::array<int>>);
    static_assert(io::detail::action_is_async(^^decltype(sequenced)::action_type));
    CHECK_EQ(calls.load(), 0);
    auto const collected = sequenced.build().sync_wait();
    CHECK_EQ(collected, (immer::array{3, 1, 2}));
    CHECK_EQ(calls.load(), 3);
    CHECK_EQ(sequenced.build().sync_wait(), collected);
    CHECK_EQ(calls.load(), 6);
    CHECK_EQ(ios[0].build().sync_wait(), 3);
}

TEST_CASE("sequence accepts empty and singleton immutable arrays")
{
    using element_type = decltype(io::just(7));
    auto const empty = io::sequence(immer::array<element_type>{});
    CHECK(empty.build().sync_wait().empty());
    CHECK_EQ(io::sequence(immer::array{io::just(7)}).build().sync_wait(), (immer::array{7}));
}

TEST_CASE("sequence consumes unique arrays and detaches shared arrays")
{
    auto const check = [](auto make_io)
    {
        std::atomic<int> copies{0};
        auto ios = immer::array{
            make_io(ArrayConsumingAction{20, &copies}), make_io(ArrayConsumingAction{22, &copies})};
        copies.store(0);
        lf::lazy_pool pool{2};

        SUBCASE("unique storage moves without copying elements")
        {
            CHECK_EQ(io::sequence(std::move(ios)).build().sync_wait(pool), (immer::array{20, 22}));
            CHECK_EQ(copies.load(), 0);
        }
        SUBCASE("shared storage copies once and leaves the original usable")
        {
            auto const shared = ios;
            CHECK_EQ(io::sequence(std::move(ios)).build().sync_wait(pool), (immer::array{20, 22}));
            CHECK_EQ(copies.load(), 2);
            CHECK_EQ(shared[0].build().sync_wait(pool), 20);
            CHECK_EQ(shared[1].build().sync_wait(pool), 22);
        }
        SUBCASE("lvalue sequences remain reusable and can later be consumed")
        {
            auto sequenced = io::sequence(std::move(ios));
            auto const & reusable = sequenced;
            CHECK_EQ(reusable.build().sync_wait(pool), (immer::array{20, 22}));
            CHECK_EQ(reusable.build().sync_wait(pool), (immer::array{20, 22}));
            CHECK_EQ(copies.load(), 4);
            CHECK_EQ(std::move(sequenced).build().sync_wait(pool), (immer::array{20, 22}));
            CHECK_EQ(copies.load(), 4);
        }
    };

    SUBCASE("synchronous")
    {
        check([](ArrayConsumingAction action) { return io::create(std::move(action)); });
    }
    SUBCASE("asynchronous")
    {
        check(
            [](ArrayConsumingAction action)
            {
                return io::create_async(
                    [](auto, ArrayConsumingAction child) -> lf::task<int>
                    { co_return std::move(child)(); },
                    std::move(action));
            });
    }
}

TEST_CASE("sequence array values need no default construction")
{
    auto const sync =
        io::sequence(immer::array{io::just(ArrayValue{20}), io::just(ArrayValue{22})});
    auto const sync_values = sync.build().sync_wait();
    REQUIRE_EQ(sync_values.size(), 2);
    CHECK_EQ(sync_values[0].value, 20);
    CHECK_EQ(sync_values[1].value, 22);

    auto const element = [](auto, int value) -> lf::task<ArrayValue>
    { co_return ArrayValue{value}; };
    auto const async = io::sequence(
        immer::array<decltype(io::create_async(element, 0))>{}
            .push_back(io::create_async(element, 20))
            .push_back(io::create_async(element, 22)));
    auto const async_values = async.build().sync_wait(lf::lazy_pool{2});
    REQUIRE_EQ(async_values.size(), 2);
    CHECK_EQ(async_values[0].value, 20);
    CHECK_EQ(async_values[1].value, 22);
}

TEST_CASE("sequence array values can be move-only")
{
    auto const sync_element = [](int value) { return io::create(ArrayMoveOnlyValueAction{value}); };
    auto const sync = io::sequence(immer::array{sync_element(20), sync_element(22)});
    auto const sync_values = sync.build().sync_wait();
    REQUIRE_EQ(sync_values.size(), 2);
    CHECK_EQ(*sync_values[0], 20);
    CHECK_EQ(*sync_values[1], 22);

    auto const async_element = [](auto, int value) -> lf::task<std::unique_ptr<int>>
    { co_return std::make_unique<int>(value); };
    auto const async = io::sequence(
        immer::array<decltype(io::create_async(async_element, 0))>{}
            .push_back(io::create_async(async_element, 20))
            .push_back(io::create_async(async_element, 22)));
    auto const async_values = async.build().sync_wait(lf::lazy_pool{2});
    REQUIRE_EQ(async_values.size(), 2);
    CHECK_EQ(*async_values[0], 20);
    CHECK_EQ(*async_values[1], 22);
}

TEST_CASE("sequence collects larger synchronous arrays in order")
{
    auto ios = immer::array<decltype(io::just(0))>{};
    for (int idx = 0; idx < 2049; ++idx)
    {
        ios = std::move(ios).push_back(io::just(idx));
    }
    auto const collected = io::sequence(std::move(ios)).build().sync_wait();
    REQUIRE_EQ(collected.size(), 2049);
    for (int idx = 0; idx < 2049; ++idx)
    {
        CHECK_EQ(collected[static_cast<std::size_t>(idx)], idx);
    }
}

TEST_CASE("sequence preserves array element values whole and accepts named and custom traits")
{
    using namespace fnfelt::literals;  // NOLINT
    auto const ios = immer::array<decltype(io::just(std::pair{1, 2}))>{}
                         .push_back(io::just(std::pair{1, 2}))
                         .push_back(io::just(std::pair{3, 4}));
    auto const named = io::sequence<"Array sequence"_ss>(ios);
    static_assert(decltype(named)::traits::name == "Array sequence");
    static_assert(std::is_same_v<decltype(named)::value_type, immer::array<std::pair<int, int>>>);
    CHECK_EQ(named.build().sync_wait()[1], (std::pair{3, 4}));
    auto const custom = io::sequence<DelegatingSequenceTraits>(ios);
    static_assert(std::is_same_v<decltype(custom)::traits, DelegatingSequenceTraits>);
    CHECK_EQ(custom.build().sync_wait(), named.build().sync_wait());
}

TEST_CASE("sequence overlaps ordinary tuple and array actions under one scheduler")
{
    std::atomic<bool> first_flag{false};
    std::atomic<bool> second_flag{false};
    struct Action
    {
        std::atomic<bool> * own;
        std::atomic<bool> * peer;
        int value;
        int operator()() const
        {
            return rendezvous(own, peer) ? value : -1000;
        }
    };
    auto const element = [](std::atomic<bool> * own, std::atomic<bool> * peer, int value)
    { return io::create(Action{own, peer, value}); };
    auto first = element(&first_flag, &second_flag, 20);
    auto second = element(&second_flag, &first_flag, 22);
    SUBCASE("tuple nested under transform and and_then")
    {
        auto collected = io::sequence(first, second)
                             .transform([](int lhs, int rhs) { return lhs + rhs; })
                             .and_then([](int total) { return io::just(total); });
        CHECK_EQ(collected.build().sync_wait(lf::lazy_pool{2}), 42);
    }
    SUBCASE("immutable array")
    {
        auto collected = io::sequence(immer::array{first, second});
        CHECK_EQ(collected.build().sync_wait(lf::lazy_pool{2}), (immer::array{20, 22}));
    }
}

TEST_CASE("sequence preserves nonassignable callable values through composed boundaries")
{
    auto produced = io::ap(
        io::just([](int offset) { return [offset](int value) { return offset + value; }; }),
        io::just(2));
    using callable_type = decltype(produced)::value_type;
    static_assert(!std::is_move_assignable_v<callable_type>);
    auto callable = produced.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(callable(40), 42);
    auto transformed = produced.transform([](auto fn) { return fn(40); });
    CHECK_EQ(transformed.build().sync_wait(lf::lazy_pool{2}), 42);
    auto sequenced = io::sequence(produced, io::just(40))
                         .transform([](auto fn, int value) { return fn(value); });
    CHECK_EQ(sequenced.build().sync_wait(lf::lazy_pool{2}), 42);
    auto chained = produced.and_then([](auto fn) { return io::just(fn(40)); });
    CHECK_EQ(chained.build().sync_wait(lf::lazy_pool{2}), 42);
    auto chained_callable = produced.and_then([](auto fn) { return io::just(fn); });
    auto result = chained_callable.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(result(40), 42);
    static_assert(
        []
        {
            auto source = io::ap(
                io::just([](int offset) { return [offset](int value) { return offset + value; }; }),
                io::just(2));
            return source.transform([](auto fn) { return fn(40); }).build().sync_wait() == 42;
        }());
}

TEST_CASE("sequence transports a move-constructible value with nonstandard assignment")
{
    struct Value
    {
        int value;
        explicit Value(int input) : value{input} {}
        Value(Value &&) = default;
        void operator=(Value && other)
        {
            value = other.value;
        }
    };
    static_assert(std::is_move_assignable_v<Value>);
    static_assert(!std::movable<Value>);
    auto source = io::sequence(io::create([] { return Value{42}; }));
    auto values = source.build().sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(values).value, 42);
    auto transformed = source.transform([](Value value) { return value.value; });
    CHECK_EQ(transformed.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("sequence overlaps async array elements under one scheduler")
{
    std::atomic<bool> first_flag{false};
    std::atomic<bool> second_flag{false};
    auto const ios = immer::array{
        io::create_async(async_rendezvous, &first_flag, &second_flag, 20),
        io::create_async(async_rendezvous, &second_flag, &first_flag, 22)};
    auto const sequenced = io::sequence(ios);
    static_assert(io::detail::action_is_async(^^decltype(sequenced)::action_type));
    CHECK_EQ(sequenced.build().sync_wait(lf::lazy_pool{2}), (immer::array{20, 22}));
    auto const summed =
        sequenced.transform([](immer::array<int> values) { return values[0] + values[1]; });
    CHECK_EQ(summed.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("sequence preserves an immutable array's memory policy")
{
    using memory_policy = immer::memory_policy<
        immer::heap_policy<immer::cpp_heap>,
        immer::refcount_policy,
        immer::spinlock_policy>;
    using element_type = decltype(io::just(7));
    auto const sync = io::sequence(immer::array<element_type, memory_policy>{io::just(7)});
    static_assert(std::is_same_v<decltype(sync)::value_type, immer::array<int, memory_policy>>);
    CHECK_EQ(sync.build().sync_wait()[0], 7);

    using async_element_type = decltype(io::create_async(async_identity, 0));
    auto const async = io::sequence<DelegatingSequenceTraits>(
        immer::array<async_element_type, memory_policy>{}
            .push_back(io::create_async(async_identity, 20))
            .push_back(io::create_async(async_identity, 22)));
    static_assert(std::is_same_v<decltype(async)::value_type, immer::array<int, memory_policy>>);
    static_assert(std::is_same_v<decltype(async)::traits, DelegatingSequenceTraits>);
    CHECK_EQ(async.build().sync_wait(lf::lazy_pool{1}), (immer::array<int, memory_policy>{20, 22}));
}

TEST_CASE("sequence handles empty, singleton and larger async arrays")
{
    using element_type = decltype(io::create_async(async_identity, 0));
    auto const empty = io::sequence(immer::array<element_type>{});
    CHECK(empty.build().sync_wait(lf::lazy_pool{1}).empty());
    auto ios = immer::array<element_type>{};
    for (int idx = 0; idx < 33; ++idx)
    {
        ios = std::move(ios).push_back(io::create_async(async_identity, idx));
    }
    auto const collected = io::sequence(std::move(ios)).build().sync_wait(lf::lazy_pool{1});
    REQUIRE_EQ(collected.size(), 33);
    for (int idx = 0; idx < 33; ++idx)
    {
        CHECK_EQ(collected[static_cast<std::size_t>(idx)], idx);
    }
    CHECK_EQ(
        io::sequence(immer::array{io::create_async(async_identity, 7)})
            .build()
            .sync_wait(lf::lazy_pool{1}),
        (immer::array{7}));
}

TEST_CASE("sequence propagates sync and async array element exceptions")
{
    auto const sync = io::sequence(immer::array{io::create(&throwing_element_action)});
    CHECK_THROWS_AS(static_cast<void>(sync.build().sync_wait()), std::runtime_error);
    std::atomic<bool> flag{false};
    auto const async = io::sequence(
        immer::array{
            io::create_async(async_throwing, &flag), io::create_async(async_throwing, &flag)});
    CHECK_THROWS_AS(
        static_cast<void>(async.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
    CHECK(flag.load(std::memory_order_relaxed));
}

TEST_CASE("sequence joins every array element before propagating an exception")
{
    std::atomic<int> calls{0};
    auto const element = [](auto, std::atomic<int> * count, int value) -> lf::task<int>
    {
        count->fetch_add(1, std::memory_order_relaxed);
        if (value == 0)
        {
            throw std::runtime_error("array element failure");
        }
        co_return value;
    };
    auto ios = immer::array<decltype(io::create_async(element, &calls, 0))>{};
    for (int idx = 0; idx < 33; ++idx)
    {
        ios = std::move(ios).push_back(io::create_async(element, &calls, idx));
    }
    auto const sequenced = io::sequence(std::move(ios));
    CHECK_THROWS_AS(
        static_cast<void>(sequenced.build().sync_wait(lf::lazy_pool{2})), std::runtime_error);
    CHECK_EQ(calls.load(std::memory_order_relaxed), 33);
}

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

TEST_CASE("sequence collects synchronous tuple results in input order")
{
    std::atomic<int> calls{0};
    auto const element = [&calls](int value)
    {
        return io::create(
            [&calls, value]
            {
                ++calls;
                return value;
            });
    };
    auto const sequenced = io::sequence(element(1), element(2), element(3));
    CHECK_EQ(calls.load(), 0);
    CHECK_EQ(sequenced.build().sync_wait(), (std::tuple{1, 2, 3}));
    CHECK_EQ(calls.load(), 3);
    CHECK_EQ(sequenced.build().sync_wait(), (std::tuple{1, 2, 3}));
    CHECK_EQ(calls.load(), 6);
}

TEST_CASE("sequence tuple values need neither copying nor default construction")
{
    auto const make_value = [] { return std::make_unique<int>(20); };
    SUBCASE("synchronous")
    {
        auto const sequenced = io::sequence(io::create(make_value), io::just(ArrayValue{22}));
        auto const collected = sequenced.build().sync_wait();
        CHECK_EQ(*std::get<0>(collected), 20);
        CHECK_EQ(std::get<1>(collected).value, 22);
    }
    SUBCASE("asynchronous")
    {
        auto sequenced = io::sequence(
            io::create_async(
                [](auto, std::unique_ptr<int> value) -> lf::task<std::unique_ptr<int>>
                { co_return std::move(value); },
                make_value()),
            io::just(ArrayValue{22}));
        auto const collected = std::move(sequenced).build().sync_wait(lf::lazy_pool{1});
        CHECK_EQ(*std::get<0>(collected), 20);
        CHECK_EQ(std::get<1>(collected).value, 22);
    }
}

TEST_CASE("sequence joins every tuple element before propagating an exception")
{
    std::atomic<int> calls{0};
    auto const failing = [&calls]() -> int
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("tuple element failure");
    };
    auto const async = [](auto, std::atomic<int> * count) -> lf::task<int>
    {
        count->fetch_add(1, std::memory_order_relaxed);
        co_return 22;
    };
    auto const sequenced =
        io::sequence(io::create(failing), io::create_async(async, &calls), io::create(failing));
    CHECK_THROWS_AS(
        static_cast<void>(sequenced.build().sync_wait(lf::lazy_pool{1})), std::runtime_error);
    CHECK_EQ(calls.load(std::memory_order_relaxed), 3);
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

TEST_CASE("sequence accepts a variadic pack of IOs")
{
    auto const sequenced = io::sequence(io::just(1), io::just(2.5), io::just(std::string{"three"}));
    static_assert(
        std::is_same_v<decltype(sequenced)::value_type, std::tuple<int, double, std::string>>);

    auto const collected = sequenced.build().sync_wait();
    CHECK_EQ(std::get<0>(collected), 1);
    CHECK_EQ(std::get<1>(collected), 2.5);
    CHECK_EQ(std::get<2>(collected), "three");

    // A single IO is a one-element pack, and a tuple argument stays on the tuple overload.
    auto const single = io::sequence(io::just(7));
    auto const tupled = io::sequence(std::make_tuple(io::just(7)));
    static_assert(std::is_same_v<decltype(single)::value_type, std::tuple<int>>);
    static_assert(std::is_same_v<decltype(tupled)::value_type, std::tuple<int>>);
}

TEST_CASE("sequence accepts a variadic pack of IOs with a name or traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto const named = io::sequence<"Variadic named"_ss>(io::just(1), io::just(2));
    static_assert(decltype(named)::traits::name == "Variadic named");
    CHECK_EQ(std::get<1>(named.build().sync_wait()), 2);

    auto const traits = io::sequence<CustomIOTraits>(io::just(1), io::just(2));
    static_assert(std::is_same_v<decltype(traits)::traits, CustomIOTraits>);
    CHECK_EQ(std::get<1>(traits.build().sync_wait()), 2);
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

TEST_CASE("sequence schedules ordinary elements at runtime")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto const sequenced = io::sequence(std::make_tuple(io::just(1), io::just(2)));
    static_assert(action_is_async(^^decltype(sequenced)::action_type));
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
    using SequenceIOTraits = io::detail::SequenceIOTraits<>;

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
    static_assert(action_is_async(^^decltype(sequenced)::action_type));
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
    using SequenceIOTraits = io::detail::SequenceIOTraits<>;

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

TEST_CASE("SequenceIOTraits::validate_sequence validates immutable array element types")
{
    using traits = io::detail::SequenceIOTraits<>;
    using array_type = immer::array<sequence_error_msg_test::FirstIO>;
    static_assert(traits::validate_sequence(^^array_type).empty());
    static_assert(traits::validate_sequence(^^array_type const &).empty());
    static_assert(
        traits::validate_sequence(^^immer::array<int>) == "element of the array is not an IO");
    static_assert(
        traits::validate_sequence(^^immer::array<io::IO<VoidElementAction>>) ==
        "element of the array produces no value");
}

TEST_CASE("detail::is_sequence_array recognises only immutable arrays")
{
    using io::detail::is_sequence_array;
    using array_type = immer::array<int>;
    static_assert(is_sequence_array(^^immer::array<int>));
    static_assert(is_sequence_array(^^array_type const &&));
    static_assert(!is_sequence_array(^^std::tuple<int>));
    static_assert(!is_sequence_array(^^int));
    static_assert(!is_sequence_array(^^io::detail::default_io_name));
}

TEST_CASE("detail::sequence_error_msg names immutable array element types")
{
    using io::detail::sequence_error_msg;
    using array_type = immer::array<sequence_error_msg_test::FirstIO>;
    static_assert(
        sequence_error_msg<^^array_type>("ArrayIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: ArrayIO{[First]}: custom reason: "} +
            std::string{display_string_of(dealias(^^sequence_error_msg_test::FirstIO))});
    static_assert(
        sequence_error_msg<^^immer::array<int>>("ArrayIO", "custom reason") ==
        "fnfelt: IO sequence error: ArrayIO{[<unknown>]}: custom reason: int");
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

    // An unsupported container is not mistaken for an empty tuple.
    static_assert(
        sequence_error_msg<^^int>("SeqIO", "custom reason") ==
        "fnfelt: IO sequence error: SeqIO{<unknown>}: custom reason: int");
    static_assert(
        sequence_error_msg<^^std::pair<int, int> const &>("SeqIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: SeqIO{<unknown>}: custom reason: "} +
            std::string{display_string_of(^^std::pair<int, int>)});
    static_assert(
        sequence_error_msg<^^io::detail::default_io_name>("SeqIO", "custom reason") ==
        std::string{"fnfelt: IO sequence error: SeqIO{<unknown>}: custom reason: "} +
            std::string{display_string_of(^^io::detail::default_io_name)});
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
