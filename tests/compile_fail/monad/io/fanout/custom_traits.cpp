// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#include <meta>

#include <string_view>

#include <fnfelt/monad/io/fanout.hpp>

namespace
{
struct Reject : fnfelt::monad::io::detail::FanoutIOTraits<>
{
    static constexpr std::string_view name = "CustomIO";
    static consteval std::string_view validate_fanout_factory(std::meta::info)
    {
        return "rejected by custom traits";
    }
};
}  // namespace

void rejected_operation()
{
    auto result = fnfelt::monad::io::fanout<Reject>();
    (void)result;
}
