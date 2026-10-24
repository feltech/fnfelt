// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/detail/lift.hpp>
#include <fnfelt/monad/reader_io/lift.hpp>

namespace
{
struct RejectLift : fnfelt::monad::reader_io::detail::LiftReaderIOTraits<>
{
    static constexpr std::string_view name = "RejectReader";

    static consteval std::string_view validate_lift(std::meta::info)
    {
        return "rejected by custom lift traits";
    }
};
}  // namespace

void lift_source()
{
    auto const reader = fnfelt::monad::reader_io::lift<int, RejectLift>(fnfelt::monad::io::just(1));
    (void)reader;
}
