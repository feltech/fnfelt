// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/and_then.hpp>
#include <fnfelt/monad/io/traits.hpp>
#include <fnfelt/static_string.hpp>

// RejectContinuation is a custom traits type whose validate_action accepts the valid action, but
// whose validate_and_then rejects every source IO and continuation pair, so and_then must fail with
// the friendly static_assert message containing the custom reason, even though the continuation
// itself is valid. Because traits are no longer inherited, the traits are passed explicitly to
// and_then.

namespace
{
/// Custom traits that accept valid actions but reject every and_then pair.
struct RejectContinuation
{
    static constexpr std::string_view name = "AndThenIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_and_then(
        std::meta::info /*io_meta*/, std::meta::info /*continuation_meta*/)
    {
        return "rejected by custom continuation traits";
    }
};
}  // namespace

/// Valid action.
int action()
{
    return 1;
}

void apply_continuation()
{
    using namespace fnfelt::literals;
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::IOTraits;

    IO<int (*)(), IOTraits<"SourceIO"_ss>> const source_io{&action};

    auto const bound = source_io.template and_then<RejectContinuation>(
        [](int) { return IO<int (*)(), IOTraits<"ResultIO"_ss>>{&action}; });
}
