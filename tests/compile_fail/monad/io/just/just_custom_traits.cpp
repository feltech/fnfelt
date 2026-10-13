// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/just.hpp>

// RejectJust is a custom traits type whose validate_action accepts the valid action, but whose
// validate_just rejects every value, so just must fail with the friendly static_assert message
// containing the custom reason and name, even though the value itself is valid. Because traits are
// passed explicitly to just, the traits' name is used rather than the default "IO".

namespace
{
/// Custom traits that accept valid actions but reject every just value.
struct RejectJust
{
    static constexpr std::string_view name = "JustIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_just(std::meta::info /*value_meta*/)
    {
        return "rejected by custom just traits";
    }
};
}  // namespace

void apply_just()
{
    auto const io = fnfelt::monad::io::just<RejectJust>(1);
}
