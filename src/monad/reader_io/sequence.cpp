// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/sequence.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <immer/array.hpp>
#include <immer/memory_policy.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

#include "../io/detail/launch_failure.hpp"

namespace
{
namespace reader = fnfelt::monad::reader_io;
namespace io = fnfelt::monad::io;
using fnfelt::literals::operator""_ss;

constexpr auto read = [](int state) { return io::just(state); };
constexpr auto read_const = [](int const & state) { return io::just(state); };

struct CountingAction
{
    std::atomic<int> * copies;
    explicit CountingAction(std::atomic<int> * copies_in) : copies{copies_in} {}
    CountingAction(CountingAction const & other) : copies{other.copies}
    {
        ++*copies;
    }
    CountingAction(CountingAction &&) = default;
    auto operator()(int state) const
    {
        return io::just(state);
    }
};

struct MoveOnlyAction
{
    std::unique_ptr<int> owned;
    auto operator()(int state) const
    {
        return io::just(state);
    }
};
using noncopy_reader_type = decltype(reader::create<int>(MoveOnlyAction{}));
using reader_type = decltype(reader::create<int>(read));
using traits_type = reader::SequenceReaderIOTraits<>;
static_assert(traits_type::validate_sequence(^^std::tuple<reader_type>, ^^int).empty());
static_assert(!traits_type::validate_sequence(^^std::tuple<reader_type &>, ^^int).empty());
static_assert(!traits_type::validate_sequence(^^std::tuple<int>, ^^int).empty());
static_assert(!traits_type::validate_sequence(^^std::tuple<reader_type>, ^^double).empty());
static_assert(!traits_type::validate_sequence(^^std::tuple<reader_type>, ^^int &).empty());
static_assert(
    traits_type::validate_sequence(^^std::tuple<reader_type &>, ^^int) ==
    "element must not be a reference type");
static_assert(
    traits_type::validate_sequence(^^immer::array<reader_type>, ^^double) ==
    "element ReaderIO must have the provided state type");
static_assert(
    traits_type::validate_sequence(^^immer::array<reader_type>, ^^std::unique_ptr<int>) ==
    "state must be copy constructible");
static_assert(
    traits_type::validate_sequence(^^immer::array<noncopy_reader_type>, ^^int) ==
    "array element ReaderIO must be copy constructible");
static_assert(traits_type::validate_sequence(^^std::tuple<>, ^^std::unique_ptr<int>).empty());
static_assert(
    !traits_type::validate_sequence(^^std::tuple<reader_type>, ^^std::unique_ptr<int>).empty());

constexpr bool sequence_constexpr()
{
    auto source = reader::sequence<int>(reader::create<int>(read), reader::just<int>(2));
    return source.build(40).sync_wait() == std::tuple{40, 2};
}
static_assert(sequence_constexpr());
}  // namespace

TEST_CASE("reader sequence binds independent environment copies before materialisation")
{
    std::atomic<int> calls{0};
    auto source = reader::create<std::string>(
        [&calls](std::string state)
        {
            ++calls;
            state += 'x';
            return io::just(state);
        });
    auto collected = reader::sequence<std::string>(source, source);
    CHECK_EQ(calls.load(), 0);
    CHECK_EQ(collected.build("a").sync_wait(), std::tuple{std::string{"ax"}, std::string{"ax"}});
    CHECK_EQ(collected.build("b").sync_wait(), std::tuple{std::string{"bx"}, std::string{"bx"}});
    CHECK_EQ(calls.load(), 4);
    auto qualified = reader::sequence<int const>(reader::create<int const>(read_const));
    CHECK_EQ(qualified.build(42).sync_wait(), std::tuple{42});
}

TEST_CASE("reader sequence supports empty inputs, names and whole tuple values")
{
    auto empty = reader::sequence<std::unique_ptr<int>>();
    CHECK_EQ(std::move(empty).build(std::make_unique<int>(1)).sync_wait(), std::tuple{});
    auto named = reader::sequence<"named readers"_ss, int>(
        std::tuple{reader::just<int>(std::tuple{1, 2}), reader::create<int>(read)});
    CHECK_EQ(named.build(40).sync_wait(), std::tuple{std::tuple{1, 2}, 40});
    static_assert(std::string_view{decltype(named)::traits::name} == "named readers");
}

TEST_CASE("reader sequence mixes async and fresh move-only synchronous values")
{
    constexpr auto length = [](auto, std::string const & state) -> lf::task<int>
    { co_return static_cast<int>(state.size()); };
    auto async = reader::create_async<std::string>(length);
    auto sync = reader::create<std::string>(
        [](std::string state)
        {
            return io::create(
                [value = std::make_unique<int>(static_cast<int>(state.size()))]() mutable
                { return std::move(value); });
        });
    auto collected = reader::sequence<std::string>(std::move(async), std::move(sync));
    auto values = collected.build(std::string(42, 'x')).sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(values), 42);
    CHECK_EQ(*std::get<1>(values), 42);
}

TEST_CASE("reader sequence preserves nonassignable callable results from composed IO")
{
    auto source = reader::create<int>(
        [](int state)
        {
            return io::sequence(io::just(state))
                .transform([](int offset)
                           { return [offset](int value) { return offset + value; }; });
        });
    using callable_type = decltype(source)::value_type;
    static_assert(!std::is_move_assignable_v<callable_type>);
    auto collected = reader::sequence<int>(source);
    auto values = collected.build(2).sync_wait(lf::lazy_pool{2});
    CHECK_EQ(std::get<0>(values)(40), 42);
}

TEST_CASE("reader array sequence preserves storage policy and retained handles")
{
    using policy_type = immer::memory_policy<
        immer::heap_policy<immer::cpp_heap>,
        immer::refcount_policy,
        immer::spinlock_policy>;
    auto source = reader::create<int>(read);
    using array_type = immer::array<decltype(source), policy_type>;
    auto inputs = array_type{source, source};
    auto retained = inputs;
    auto collected = reader::sequence<int>(std::move(inputs));
    static_assert(
        std::is_same_v<typename decltype(collected)::value_type, immer::array<int, policy_type>>);
    CHECK_EQ(collected.build(42).sync_wait(), (immer::array<int, policy_type>{42, 42}));
    CHECK_EQ(collected.build(43).sync_wait(), (immer::array<int, policy_type>{43, 43}));
    CHECK_EQ(retained.size(), 2);
    CHECK_EQ(retained[0].build(44).sync_wait(), 44);
    auto empty = reader::sequence<int>(array_type{});
    CHECK(empty.build(1).sync_wait().empty());
}

TEST_CASE("reader array sequence accepts async move-only results")
{
    constexpr auto leaf = [](auto, int state) -> lf::task<std::unique_ptr<int>>
    { co_return std::make_unique<int>(state); };
    auto source = reader::create_async<int>(leaf);
    auto collected = reader::sequence<int>(immer::array{source, source});
    auto values = std::move(collected).build(42).sync_wait(lf::lazy_pool{2});
    REQUIRE_EQ(values.size(), 2);
    CHECK_EQ(*values[0], 42);
    CHECK_EQ(*values[1], 42);
}

TEST_CASE("reader array sequence copies elements only when detaching shared storage")
{
    std::atomic<int> copies{0};
    auto source = reader::create<int>(CountingAction{&copies});
    auto inputs = immer::array{source, source};
    SUBCASE("unique")
    {
        auto collected = reader::sequence<int>(std::move(inputs));
        copies = 0;
        auto values = std::move(collected).build(42).sync_wait();
        CHECK_EQ(copies.load(), 0);
        CHECK_EQ(values, (immer::array{42, 42}));
    }
    SUBCASE("shared")
    {
        auto retained = inputs;
        auto collected = reader::sequence<int>(std::move(inputs));
        copies = 0;
        auto values = std::move(collected).build(42).sync_wait();
        CHECK_EQ(copies.load(), 2);
        CHECK_EQ(values, (immer::array{42, 42}));
        CHECK_EQ(retained.size(), 2);
    }
}

TEST_CASE("reader tuple sequence consumes move-only source actions")
{
    auto source = reader::create<int>([owned = std::make_unique<int>(2)](int state)
                                      { return io::just(*owned + state); });
    auto collected = reader::sequence<int>(std::move(source));
    CHECK_EQ(std::move(collected).build(40).sync_wait(), std::tuple{42});
}

TEST_CASE("reader sequence overlaps ordinary sources and retains result ordering")
{
    namespace failure = launch_failure_test;
    failure::State state;
    struct Action
    {
        failure::State * shared;
        bool is_first;
        int operator()() const
        {
            auto & own = is_first ? shared->started : shared->release;
            auto & peer = is_first ? shared->release : shared->started;
            own.store(true, std::memory_order_release);
            if (!failure::wait(peer))
                shared->timed_out.store(true);
            return is_first ? 20 : 22;
        }
    };
    struct ReaderAction
    {
        bool is_first;
        auto operator()(failure::State * shared) const
        {
            return io::create(Action{shared, is_first});
        }
    };
    auto first = reader::create<failure::State *>(ReaderAction{true});
    auto second = reader::create<failure::State *>(ReaderAction{false});
    SUBCASE("tuple")
    {
        auto collected = reader::sequence<failure::State *>(first, second);
        CHECK_EQ(collected.build(&state).sync_wait(lf::lazy_pool{2}), std::tuple{20, 22});
    }
    SUBCASE("array")
    {
        auto collected = reader::sequence<failure::State *>(immer::array{first, second});
        CHECK_EQ(collected.build(&state).sync_wait(lf::lazy_pool{2}), (immer::array{20, 22}));
    }
    CHECK_FALSE(state.timed_out.load());
}

TEST_CASE("reader sequence overlaps asynchronous sources and retains result ordering")
{
    namespace failure = launch_failure_test;
    failure::State state;
    constexpr auto leaf = [](auto, failure::State * shared, std::string position) -> lf::task<int>
    {
        auto const is_first = position == "first";
        auto & own = is_first ? shared->started : shared->release;
        auto & peer = is_first ? shared->release : shared->started;
        own.store(true, std::memory_order_release);
        if (!failure::wait(peer))
        {
            shared->timed_out.store(true, std::memory_order_relaxed);
        }
        co_return is_first ? 20 : 22;
    };
    auto collected = reader::sequence<failure::State *>(
        reader::create_async<failure::State *>(leaf, std::string{"first"}),
        reader::create_async<failure::State *>(leaf, std::string{"second"}));
    CHECK_EQ(collected.build(&state).sync_wait(lf::lazy_pool{2}), std::tuple{20, 22});
    CHECK_FALSE(state.timed_out.load(std::memory_order_relaxed));
    failure::State array_state;
    auto first = reader::create_async<failure::State *>(leaf, std::string{"first"});
    auto second = reader::create_async<failure::State *>(leaf, std::string{"second"});
    std::vector sources{first, second};
    auto array_collected = reader::sequence<failure::State *>(
        immer::array<decltype(first)>{sources.begin(), sources.end()});
    CHECK_EQ(
        array_collected.build(&array_state).sync_wait(lf::lazy_pool{2}), (immer::array{20, 22}));
    CHECK_FALSE(array_state.timed_out.load(std::memory_order_relaxed));
}

#endif  // DOCTEST_CONFIG_DISABLE
