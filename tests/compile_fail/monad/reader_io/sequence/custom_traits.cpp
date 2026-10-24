// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <expected>
#include <string_view>
#include <tuple>

#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/monad/reader_io/sequence.hpp>

namespace
{
struct Reject : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    static consteval std::string_view validate_sequence(std::meta::info, std::meta::info)
    {
        return "rejected by custom sequence traits";
    }
};
}  // namespace

void rejected_operation()
{
    auto result = fnfelt::monad::reader_io::sequence<int, Reject>(std::tuple{});
    (void)result;
}
