// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/traits.hpp>
#include <fnfelt/monad/io/transform.hpp>

// The source is non-IO and the transformer is non-callable, but both must still reach
// RejectTransformSource::validate_transform. Transform must fail with the friendly static_assert
// message containing the custom reason and name, rather than rejecting either argument during
// overload resolution.

namespace
{
/// Custom traits that accept valid actions but reject every transform pair.
struct RejectTransformSource
{
    static constexpr std::string_view name = "Named transform";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_transform(
        std::meta::info /*io_meta*/, std::meta::info /*transformer_meta*/)
    {
        return "rejected by custom transform source traits";
    }
};
}  // namespace

void apply_non_io()
{
    auto const transformed = fnfelt::monad::io::transform<RejectTransformSource>(42, 43);
    (void)transformed;
}
