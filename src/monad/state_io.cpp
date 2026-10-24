// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE
#include <fnfelt/monad/state_io.hpp>

#include <doctest/doctest.h>

#include <tuple>
#include <utility>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io.hpp>

TEST_CASE("StateIO umbrella exposes a state-threading pipeline")
{
    namespace state = fnfelt::monad::state_io;
    namespace io = fnfelt::monad::io;
    constexpr auto program =
        state::create<int>([](int counter) { return io::just(std::pair{counter, counter + 1}); })
            .and_then([](int previous) { return state::just<int>(previous * 2); });
    static_assert(program.build(4).sync_wait() == std::pair{8, 5});
    CHECK_EQ(program.build(4).sync_wait(), (std::pair{8, 5}));
}

TEST_CASE("StateIO named operations preserve diagnostic traits through compositions")
{
    using fnfelt::literals::operator""_ss;
    namespace state = fnfelt::monad::state_io;
    constexpr auto source = state::just<"source"_ss, int>(2);
    constexpr auto mapped = source.transform<"mapped"_ss>([](int value) { return value * 2; });
    constexpr auto chained =
        mapped.and_then<"chained"_ss>([](int value) { return state::just<int>(value); });
    constexpr auto collected = state::sequence<"collected"_ss, int>(source, chained);
    static_assert(decltype(source)::traits::name == "source");
    static_assert(decltype(mapped)::traits::name == "mapped");
    static_assert(decltype(chained)::traits::name == "chained");
    static_assert(decltype(collected)::traits::name == "collected");
    static_assert(collected.build(4).sync_wait() == std::pair{std::tuple{2, 4}, 4});
    auto reader = fnfelt::monad::reader_io::just<int>(2);
    CHECK_EQ(state::lift<int>(reader).build(4).sync_wait(), (std::pair{2, 4}));
}
#endif  // DOCTEST_CONFIG_DISABLE
