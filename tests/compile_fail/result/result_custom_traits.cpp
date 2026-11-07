// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/result.hpp>

namespace
{
/// Injected validation proving Result reports the custom reason.
struct RejectResult
{
    static consteval std::string_view validate(std::meta::info, std::meta::info)
    {
        return "rejected by custom result traits";
    }
};
}  // namespace

void rejected_result()
{
    [[maybe_unused]]
    typename fnfelt::detail::ResultType<int, fnfelt::Infallible, RejectResult>::type result;
}
