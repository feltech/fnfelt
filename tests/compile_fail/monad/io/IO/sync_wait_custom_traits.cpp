// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/create.hpp>

#include <meta>

#include <string_view>
#include <vector>

namespace
{
/// Custom traits rejecting execution of an otherwise valid IO.
struct RejectSyncWait : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "CustomRunIO";

    static consteval std::string_view validate_sync_wait(
        std::meta::info /*self_meta*/,
        std::meta::info /*action_meta*/,
        std::vector<std::meta::info> const & /*scheduler_metas*/)
    {
        return "rejected by custom sync_wait traits";
    }
};
}  // namespace

void run()
{
    (void)fnfelt::monad::io::create<RejectSyncWait>([] { return 1; }).build().sync_wait();
}
