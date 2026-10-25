// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <string_view>

#include <fnfelt/monad/reader_io/fanout.hpp>

namespace
{
struct Reject : fnfelt::monad::reader_io::detail::FanoutReaderIOTraits<>
{
    static constexpr std::string_view name = "CustomReaderIO";
    static consteval std::string_view validate_fanout_factory(std::meta::info, std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    auto result = fnfelt::monad::reader_io::fanout<int, Reject>();
    (void)result;
}
