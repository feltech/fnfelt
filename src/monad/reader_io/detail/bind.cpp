// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/detail/bind.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/repeat_until.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/create.hpp>

#include "../../io/detail/launch_failure.hpp"

// NOLINTBEGIN(*-magic-numbers)
namespace
{
struct State
{
    int value;
};

struct QualifiedAction
{
    constexpr auto operator()(State && state) const
    {
        return fnfelt::monad::io::just(state.value + 1);
    }
    constexpr auto operator()(State const & state) const
    {
        return fnfelt::monad::io::just(state.value + 2);
    }
};

constexpr int bind_constexpr()
{
    auto reader = fnfelt::monad::reader_io::create<int>(
        [](int state) { return fnfelt::monad::io::just(state * 2); });
    return fnfelt::monad::reader_io::detail::bind_reader<int>(reader, 21).build().sync_wait();
}
}  // namespace

TEST_CASE("bound readers defer materialisation and repeat it on every IO execution")
{
    int materialisations = 0;
    auto source = fnfelt::monad::reader_io::create<int>(
        [&materialisations](int state)
        {
            ++materialisations;
            return fnfelt::monad::io::just(state);
        });
    auto bound = fnfelt::monad::reader_io::detail::bind_reader<int>(source, 42);
    CHECK_EQ(materialisations, 0);
    CHECK_EQ(bound.build().sync_wait(), 42);
    CHECK_EQ(bound.build().sync_wait(), 42);
    CHECK_EQ(materialisations, 2);
    static_assert(bind_constexpr() == 42);
}

TEST_CASE("bound readers preserve environment cv qualification")
{
    auto source = fnfelt::monad::reader_io::create<State const>(QualifiedAction{});
    auto bound = fnfelt::monad::reader_io::detail::bind_reader<State const>(source, State{40});
    CHECK_EQ(bound.build().sync_wait(), 42);
}

TEST_CASE("copyable bound readers can create fresh move-only inner IOs per iteration")
{
    int materialisations = 0;
    auto source = fnfelt::monad::reader_io::create<int>(
        [&materialisations](int state)
        {
            ++materialisations;
            return fnfelt::monad::io::create([value = std::make_unique<int>(state)]() mutable
                                             { return std::move(value); });
        });
    auto bound = fnfelt::monad::reader_io::detail::bind_reader<int>(source, 7);
    auto repeated = fnfelt::monad::io::repeat_until(
        std::move(bound),
        0,
        [](int count, std::unique_ptr<int> value) -> std::expected<int, int>
        {
            if (count == 2)
            {
                return *value;
            }
            return std::unexpected{count + 1};
        });
    CHECK_EQ(repeated.build().sync_wait(), 7);
    CHECK_EQ(materialisations, 3);
}

TEST_CASE("bound asynchronous readers retain referenced environments in the shared IO tree")
{
    constexpr auto length = [](auto, std::string const & state) -> lf::task<int>
    { co_return static_cast<int>(state.size()); };
    auto source = fnfelt::monad::reader_io::create_async<std::string>(length);
    auto bound = fnfelt::monad::reader_io::detail::bind_reader<std::string>(
        std::move(source), std::string(200, 'x'));
    auto mapped = std::move(bound).transform([](int size) { return size + 1; });
    CHECK_EQ(mapped.build().sync_wait(lf::lazy_pool{2}), 201);
}

TEST_CASE("bound readers retain environments borrowed by freshly materialised inner actions")
{
    auto source = fnfelt::monad::reader_io::create<std::string>(
        [](std::string const & state)
        { return fnfelt::monad::io::create([&state] { return static_cast<int>(state.size()); }); });
    auto bound = fnfelt::monad::reader_io::detail::bind_reader<std::string>(
        std::move(source), std::string(200, 'x'));
    CHECK_EQ(std::move(bound).build().sync_wait(), 200);
}

TEST_CASE("bound reader materialisation exceptions join live asynchronous siblings")
{
    namespace failure = launch_failure_test;
    failure::State state;
    constexpr auto child = [](auto, failure::State * shared) -> lf::task<int>
    {
        shared->started.store(true, std::memory_order_release);
        if (!failure::wait(shared->release))
        {
            shared->timed_out.store(true, std::memory_order_relaxed);
        }
        shared->finished.store(true, std::memory_order_release);
        co_return 1;
    };
    auto first = fnfelt::monad::reader_io::create<failure::State *>(
        [child](failure::State * shared)
        { return fnfelt::monad::io::create_async(child, shared); });
    auto second = fnfelt::monad::reader_io::create<failure::State *>(
        [child](failure::State * shared) -> decltype(fnfelt::monad::io::create_async(child, shared))
        {
            if (!failure::wait(shared->started))
            {
                shared->timed_out.store(true, std::memory_order_relaxed);
            }
            shared->release.store(true, std::memory_order_release);
            throw std::runtime_error("reader materialisation failure");
        });
    auto combined = fnfelt::monad::io::sequence(
        fnfelt::monad::reader_io::detail::bind_reader<failure::State *>(std::move(first), &state),
        fnfelt::monad::reader_io::detail::bind_reader<failure::State *>(std::move(second), &state));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(std::move(combined).build().sync_wait(lf::lazy_pool{2})),
        "reader materialisation failure",
        std::runtime_error);
    CHECK(state.finished.load(std::memory_order_acquire));
    CHECK_FALSE(state.timed_out.load(std::memory_order_relaxed));
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
