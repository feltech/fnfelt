// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/traits.hpp>
#include <fnfelt/monad/io/transform.hpp>
#include <fnfelt/static_string.hpp>

// RejectTransformer is a custom traits type whose validate_action accepts the valid action, but
// whose validate_transform rejects every source IO and transformer pair, so transform must fail
// with the friendly static_assert message containing the custom reason, even though the transformer
// itself is valid. Because traits are no longer inherited, the traits are passed explicitly to
// transform.

namespace
{
/// Custom traits that accept valid actions but reject every transform pair.
struct RejectTransformer
{
    static constexpr std::string_view name = "TransformIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_transform(
        std::meta::info /*io_meta*/, std::meta::info /*transformer_meta*/)
    {
        return "rejected by custom transformer traits";
    }
};
}  // namespace

/// Valid action.
int action()
{
    return 1;
}

void apply_transformer()
{
    using namespace fnfelt::literals;
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    IO<int (*)(), IOTraits<"SourceIO"_ss>> const source_io{&action};

    auto const transformed = source_io.template transform<RejectTransformer>([](int) { return 1; });
}
