// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/bind.hpp>
#include <fnfelt/monad/io/traits.hpp>
#include <fnfelt/static_string.hpp>

// RejectKleisli is a custom traits type whose validate_action accepts the valid action, but whose
// validate_bind rejects every source IO and continuation pair, so bind must fail with the friendly
// static_assert message containing the custom reason, even though the kleisli itself is valid.
// Because traits are no longer inherited, the traits are passed explicitly to bind.

namespace
{
/// Custom traits that accept valid actions but reject every bind pair.
struct RejectKleisli
{
    static constexpr std::string_view name = "BoundIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_bind(
        std::meta::info /*io_meta*/, std::meta::info /*kleisli_meta*/)
    {
        return "rejected by custom kleisli traits";
    }
};
}  // namespace

/// Valid action.
int action()
{
    return 1;
}

void bind_kleisli()
{
    using namespace fnfelt::literals;
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    IO<int (*)(), IOTraits<"SourceIO"_ss>> const source_io{&action};

    auto const bound = source_io.template bind<RejectKleisli>(
        [](int) { return IO<int (*)(), IOTraits<"ResultIO"_ss>>{&action}; });
}
