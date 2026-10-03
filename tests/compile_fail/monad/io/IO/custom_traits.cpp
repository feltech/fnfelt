// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/create.hpp>

#include <meta>

#include <string_view>

// RejectAll is a custom traits type that rejects every action, so create must fail with the
// friendly static_assert message containing the custom reason, even though the action itself is
// valid.

namespace
{
/// Custom traits that reject every action.
struct RejectAll
{
    static constexpr std::string_view name = "IO";

    static consteval std::string_view validate_action(std::meta::info /*action_meta*/)
    {
        return "rejected by custom action traits";
    }
};

/// Valid action.
int action()
{
    return 1;
}
}  // namespace

void construct()
{
    auto const io_monad = fnfelt::monad::io::create<RejectAll>(&action);
}
