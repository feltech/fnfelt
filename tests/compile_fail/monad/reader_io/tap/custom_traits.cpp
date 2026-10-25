// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <string_view>

#include <fnfelt/monad/reader_io/just.hpp>
#include <fnfelt/monad/reader_io/tap.hpp>

namespace
{
struct Reject : fnfelt::monad::reader_io::detail::TapReaderIOTraits<>
{
    static constexpr std::string_view name = "CustomReaderIO";
    static consteval std::string_view validate_tap(std::meta::info, std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    namespace io = fnfelt::monad::reader_io;
    auto result = io::tap<Reject>(io::just<int>(1), io::just<int>(2));
    (void)result;
}
