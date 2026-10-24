// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <expected>
#include <string_view>
#include <tuple>

#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/monad/reader_io/repeat_until.hpp>

namespace
{
struct Reject : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    static consteval std::string_view validate_repeat_until(
        std::meta::info, std::meta::info, std::meta::info)
    {
        return "rejected by custom repeat_until traits";
    }
};
}  // namespace

void rejected_operation()
{
    auto result = fnfelt::monad::reader_io::repeat_until<Reject>(
        fnfelt::monad::reader_io::just<int>(1),
        0,
        [](int count, int value) -> std::expected<int, int> { return count + value; });
    (void)result;
}
