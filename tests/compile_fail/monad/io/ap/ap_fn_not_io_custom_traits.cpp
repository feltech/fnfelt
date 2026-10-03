// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/traits.hpp>

// Both arguments are non-IO values, but must still reach RejectApSource::validate_ap.
// Ap must fail with the friendly static_assert message containing the custom reason and name,
// rather than rejecting either argument during overload resolution.

namespace
{
/// Custom traits that accept valid actions but reject every ap pair.
struct RejectApSource
{
    static constexpr std::string_view name = "Named ap";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_ap(
        std::meta::info /*fn_io_meta*/, std::meta::info /*value_io_meta*/)
    {
        return "rejected by custom ap source traits";
    }
};
}  // namespace

void ap_non_io_fn()
{
    auto const applied = fnfelt::monad::io::ap<RejectApSource>(42, 43);
    (void)applied;
}
