// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/result.hpp>

namespace
{
/// Injected validation proving owned errors report the custom reason.
struct RejectErrorTraits
{
    static consteval std::string_view validate(std::meta::info)
    {
        return "rejected by custom error traits";
    }
};
}  // namespace

void rejected_error()
{
    using error_type = fnfelt::detail::ErrorType<int, RejectErrorTraits>::type;
    static_assert(sizeof(error_type) > 0);
}
