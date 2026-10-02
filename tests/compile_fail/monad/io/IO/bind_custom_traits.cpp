// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/monad/io/IO.hpp>

#include <meta>

#include <string_view>

// RejectKleisli is a custom traits type whose validate_action accepts the valid action, but whose
// validate_kleisli rejects every continuation, so bind must fail with the friendly static_assert
// message containing the custom reason, even though the kleisli itself is valid.

namespace
{
/// Custom traits that accept valid actions but reject every kleisli.
struct RejectKleisli
{
    static constexpr std::string_view name = "IO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_kleisli(
        std::meta::info /*value_meta*/, std::meta::info /*kleisli_meta*/)
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
    fnfelt::monad::io::IO<int (*)(), RejectKleisli> const io_monad{&action};
    auto const bound =
        io_monad.bind([](int x) { return fnfelt::monad::io::IO{[x] { return x + 1; }}; });
}