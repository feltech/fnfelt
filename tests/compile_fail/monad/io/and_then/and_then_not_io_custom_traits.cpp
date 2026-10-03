// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/io/traits.hpp>

// The source is non-IO and the continuation is non-callable, but both must still reach
// RejectAndThenSource::validate_and_then. And_then must fail with the friendly static_assert
// message containing the custom reason and name, rather than rejecting either argument during
// overload resolution.

namespace
{
/// Custom traits that accept valid actions but reject every and_then pair.
struct RejectAndThenSource
{
    static constexpr std::string_view name = "Named and_then";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_and_then(
        std::meta::info /*io_meta*/, std::meta::info /*continuation_meta*/)
    {
        return "rejected by custom and_then source traits";
    }
};
}  // namespace

void apply_non_io()
{
    auto const bound = fnfelt::monad::io::and_then<RejectAndThenSource>(42, 43);
    (void)bound;
}
