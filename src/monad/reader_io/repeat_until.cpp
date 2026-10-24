// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/repeat_until.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/create.hpp>
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

struct Step
{
    constexpr auto operator()(int count, int value) & -> std::expected<int, int>
    {
        if (count == 2)
        {
            return value;
        }
        return std::unexpected{count + 1};
    }
};

constexpr int repeat_constexpr()
{
    auto result = reader::just<int>(42).repeat_until(0, Step{});
    return result.build(7).sync_wait();
}
static_assert(repeat_constexpr() == 42);
using traits_type = reader::detail::RepeatUntilReaderIOTraits<>;
using source_type = decltype(reader::just<int>(1));
struct MoveOnlyAction
{
    std::unique_ptr<int> owned;
    auto operator()(int state) const
    {
        return io::just(state);
    }
};
using noncopy_reader_type = decltype(reader::create<int>(MoveOnlyAction{}));
using noncopy_env_type = decltype(reader::just<std::unique_ptr<int>>(1));
static_assert(traits_type::validate_repeat_until(^^source_type, ^^int, ^^Step).empty());
static_assert(!traits_type::validate_repeat_until(^^int, ^^int, ^^Step).empty());
static_assert(!traits_type::validate_repeat_until(^^source_type, ^^std::string, ^^Step).empty());
static_assert(
    traits_type::validate_repeat_until(^^noncopy_reader_type, ^^int, ^^Step) ==
    "source ReaderIO must be copy constructible");
static_assert(
    traits_type::validate_repeat_until(^^noncopy_env_type, ^^int, ^^Step) ==
    "state must be copy constructible");
}  // namespace

TEST_CASE("reader repeat_until rematerialises its source with an isolated environment each time")
{
    int materialisations = 0;
    auto source = reader::create<std::string>(
        [&materialisations](std::string state)
        {
            ++materialisations;
            state += 'x';
            return io::create([owned = std::make_unique<std::string>(std::move(state))]() mutable
                              { return std::move(owned); });
        });
    auto result = reader::repeat_until<"fresh iterations"_ss>(
        source,
        std::make_unique<int>(0),
        [](std::unique_ptr<int> count,
           std::unique_ptr<std::string> value) -> std::expected<std::string, std::unique_ptr<int>>
        {
            CHECK_EQ(*value, "envx");
            if (++*count == 3)
            {
                return std::move(*value);
            }
            return std::unexpected{std::move(count)};
        });
    CHECK_EQ(materialisations, 0);
    CHECK_EQ(std::move(result).build("env").sync_wait(), "envx");
    CHECK_EQ(materialisations, 3);
}

TEST_CASE("reader repeat_until supports async borrowed state and move-only steppers")
{
    constexpr auto length = [](auto, std::string const & state) -> lf::task<int>
    { co_return static_cast<int>(state.size()); };
    auto result = reader::create_async<std::string>(length).repeat_until<"async loop"_ss>(
        0,
        [limit = std::make_unique<int>(2)](int count, int value) mutable -> std::expected<int, int>
        {
            if (count == *limit)
            {
                return value;
            }
            return std::unexpected{count + 1};
        });
    CHECK_EQ(std::move(result).build(std::string(42, 'x')).sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("reader repeat_until handles a long iterative loop and replay")
{
    auto result = reader::just<int>(42).repeat_until(
        0,
        [](int count, int value) -> std::expected<int, int>
        {
            if (count == 10000)
            {
                return value;
            }
            return std::unexpected{count + 1};
        });
    CHECK_EQ(result.build(7).sync_wait(), 42);
    CHECK_EQ(result.build(8).sync_wait(), 42);
}

#endif  // DOCTEST_CONFIG_DISABLE
