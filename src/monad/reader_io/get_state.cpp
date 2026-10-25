// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/reader_io/get_state.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <fnfelt/static_string.hpp>

namespace
{
struct Incomplete;
struct Immovable
{
    Immovable() = default;
    Immovable(Immovable &&) = delete;
};
}  // namespace

TEST_CASE("ReaderIO get_state returns independent environments and preserves traits")
{
    using fnfelt::literals::operator""_ss;
    namespace reader_io = fnfelt::monad::reader_io;
    constexpr auto action = reader_io::get_state<int>();
    static_assert(action.build(4).sync_wait() == 4);
    CHECK_EQ(action.build(4).sync_wait(), 4);
    CHECK_EQ(action.build(7).sync_wait(), 7);
    constexpr auto named = reader_io::get_state<"named"_ss, int>();
    static_assert(named.build(5).sync_wait() == 5);
    static_assert(decltype(named)::traits::name == "named");
    using traits_type = reader_io::detail::GetStateReaderIOTraits<>;
    static_assert(traits_type::validate_get_state(^^int).empty());
    static_assert(!traits_type::validate_get_state(^^void).empty());
    static_assert(!traits_type::validate_get_state(^^int &).empty());
    static_assert(!traits_type::validate_get_state(^^Incomplete).empty());
    static_assert(!traits_type::validate_get_state(^^Immovable).empty());
}

TEST_CASE("ReaderIO get_state supports move-only environments")
{
    namespace reader_io = fnfelt::monad::reader_io;
    auto action = reader_io::get_state<std::unique_ptr<int>>();
    static_assert(std::is_same_v<decltype(action)::value_type, std::unique_ptr<int>>);
    auto environment = std::make_unique<int>(4);
    auto const * original = environment.get();
    auto proxy = action.build(std::move(environment));
    CHECK_FALSE(environment);
    auto result = std::move(proxy).sync_wait();
    CHECK_EQ(result.get(), original);
    CHECK_EQ(*result, 4);
    CHECK_EQ(*action.build(std::make_unique<int>(7)).sync_wait(), 7);
}
#endif  // DOCTEST_CONFIG_DISABLE
