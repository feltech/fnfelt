// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/traverse.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/static_string.hpp>

namespace
{
namespace reader = fnfelt::monad::reader_io;
namespace io = fnfelt::monad::io;
using fnfelt::literals::operator""_ss;

struct Dispatch
{
    constexpr auto operator()(std::pair<int, int> value) &
    {
        return reader::just<int>(value.first + value.second);
    }
    constexpr auto operator()(int, int) &
    {
        return reader::just<int>(-1);
    }
};

constexpr bool traverse_constexpr()
{
    auto result = reader::traverse<int>(std::make_tuple(std::pair{20, 22}), Dispatch{});
    return result.build(0).sync_wait() == std::tuple{42};
}
static_assert(traverse_constexpr());
using traits_type = reader::detail::TraverseReaderIOTraits<>;
struct PlainResult
{
    int operator()(int value) &;
};
struct DifferentState
{
    auto operator()(int value) &
    {
        return reader::just<double>(value);
    }
};
static_assert(traits_type::validate_traverse(^^std::tuple<>, ^^Dispatch, ^^int).empty());
static_assert(!traits_type::validate_traverse(^^int, ^^Dispatch, ^^int).empty());
static_assert(!traits_type::validate_traverse(^^std::tuple<int>, ^^Dispatch, ^^int).empty());
static_assert(
    traits_type::validate_traverse(^^std::tuple<int>, ^^int, ^^int) ==
    "continuation must be a complete non-reference class or function pointer");
static_assert(
    traits_type::validate_traverse(^^std::tuple<int>, ^^PlainResult, ^^int) ==
    "element is not a ReaderIO");
static_assert(
    traits_type::validate_traverse(^^std::tuple<int>, ^^DifferentState, ^^int) ==
    "element ReaderIO must have the provided state type");
}  // namespace

TEST_CASE("reader traverse maps once as a mutable lvalue and rematerialises readers on execution")
{
    int mapped = 0;
    std::atomic<int> materialised{0};
    auto result = reader::traverse<int>(
        std::tuple{1, 2},
        [owned = std::make_unique<int>(40), &mapped, &materialised](int value) mutable
        {
            ++mapped;
            return reader::create<int>(
                [value, offset = *owned, &materialised](int state)
                {
                    ++materialised;
                    return io::just(value + offset + state);
                });
        });
    CHECK_EQ(mapped, 2);
    CHECK_EQ(materialised.load(), 0);
    CHECK_EQ(result.build(0).sync_wait(), std::tuple{41, 42});
    CHECK_EQ(result.build(1).sync_wait(), std::tuple{42, 43});
    CHECK_EQ(mapped, 2);
    CHECK_EQ(materialised.load(), 4);
}

TEST_CASE("reader traverse supports spreading, references and empty move-only environments")
{
    auto spread = reader::traverse<int>(
        std::make_tuple(std::pair{20, 22}),
        [](int left, int right) { return reader::just<int>(left + right); });
    CHECK_EQ(spread.build(0).sync_wait(), std::tuple{42});
    auto source = reader::just<int>(42);
    auto reference = reader::traverse<int>(
        std::tuple{1}, [&source](int) -> decltype(source) & { return source; });
    CHECK_EQ(reference.build(0).sync_wait(), std::tuple{42});
    auto empty = reader::traverse<std::unique_ptr<int>>(
        std::tuple{}, [](int) { return reader::just<int>(1); });
    CHECK_EQ(std::move(empty).build(std::make_unique<int>(1)).sync_wait(), std::tuple{});
}

TEST_CASE("named reader traverse composes async leaves under its scheduler")
{
    constexpr auto leaf = [](auto, int state, int offset) -> lf::task<int>
    { co_return state + offset; };
    auto result = reader::traverse<"async traverse"_ss, int>(
        std::tuple{1, 2}, [leaf](int value) { return reader::create_async<int>(leaf, value); });
    CHECK_EQ(result.build(40).sync_wait(lf::lazy_pool{2}), std::tuple{41, 42});
}

#endif  // DOCTEST_CONFIG_DISABLE
