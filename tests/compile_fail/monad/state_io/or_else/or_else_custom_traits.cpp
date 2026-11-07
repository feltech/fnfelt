// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/state_io.hpp>
#include <fnfelt/result.hpp>

namespace
{
struct RejectRecovery : fnfelt::monad::state_io::detail::OrElseStateIOTraits<>
{
    static constexpr std::string_view name = "RejectState";

    static consteval std::string_view validate_or_else(std::meta::info, std::meta::info)
    {
        return "rejected by custom recovery traits";
    }
};
}  // namespace

void rejected_recovery()
{
    auto source =
        fnfelt::monad::state_io::just<int>(fnfelt::result::success<fnfelt::Result<int, int>>(1));
    auto recovered = source.or_else<RejectRecovery>(
        [](int error) { return fnfelt::monad::state_io::just<int>(error); });
    (void)recovered;
}
