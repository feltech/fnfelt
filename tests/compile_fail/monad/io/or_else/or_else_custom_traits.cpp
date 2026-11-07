// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/result.hpp>

namespace
{
/// Injected validation proving the public entry point reports the custom reason.
struct RejectRecovery : fnfelt::monad::io::detail::OrElseIOTraits<>
{
    static constexpr std::string_view name = "RecoveryIO";

    static consteval std::string_view validate_or_else(std::meta::info, std::meta::info)
    {
        return "rejected by custom recovery traits";
    }
};
}  // namespace

void rejected_recovery()
{
    auto source = fnfelt::monad::io::create(
        [] { return fnfelt::Result<int, int>{std::in_place, std::in_place, 1}; });
    auto recovered = source.or_else<RejectRecovery>(
        [](int error) { return fnfelt::monad::io::create([error] { return error; }); });
}
