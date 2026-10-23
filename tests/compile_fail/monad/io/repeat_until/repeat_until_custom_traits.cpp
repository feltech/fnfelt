// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <expected>

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/repeat_until.hpp>
#include <fnfelt/static_string.hpp>

// RejectRepeatUntil is a custom traits type whose validate_action accepts the valid action, but
// whose validate_repeat_until rejects every source IO, state and stepper combination, so
// repeat_until must fail with the friendly static_assert message containing the custom reason,
// even though the combination itself is valid. Because traits are no longer inherited, the traits
// are passed explicitly to repeat_until.

namespace
{
/// Custom traits that accept valid actions but reject every repeat_until combination.
struct RejectRepeatUntil
{
    static constexpr std::string_view name = "RepeatUntilIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_repeat_until(
        std::meta::info /*io_meta*/,
        std::meta::info /*state_meta*/,
        std::meta::info /*stepper_meta*/)
    {
        return "rejected by custom repeat traits";
    }
};
}  // namespace

/// Valid action.
int action()
{
    return 1;
}

void apply_repeat_until()
{
    using namespace fnfelt::literals;
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::CreateIOTraits;
    using fnfelt::monad::io::detail::RepeatUntilIOTraits;

    IO<int (*)(), fnfelt::monad::io::detail::CreateIOTraits<"SourceIO"_ss>> const source_io{
        &action};

    auto const repeated = source_io.template repeat_until<RejectRepeatUntil>(
        0, [](int, int) { return std::expected<int, int>{1}; });
}
