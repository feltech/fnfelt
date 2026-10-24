// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io.hpp>

#include <doctest/doctest.h>

#include <string>
#include <tuple>

#include <fnfelt/monad/io.hpp>

TEST_CASE("ReaderIO umbrella composes an environment-dependent calculation")
{
    namespace reader = fnfelt::monad::reader_io;
    namespace io = fnfelt::monad::io;
    struct Config
    {
        int base;
        std::string label;
    };
    auto calculation = reader::create<Config>([](Config config) { return io::just(config.base); })
                           .and_then([](int base) { return reader::just<Config>(base + 2); });
    CHECK_EQ(calculation.build(Config{40, "answer"}).sync_wait(), 42);
}

#endif  // DOCTEST_CONFIG_DISABLE
