// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>
#include <tuple>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/sequence.hpp>
#include <fnfelt/static_string.hpp>

// RejectSequence is a custom traits type whose validate_action accepts the valid actions, but whose
// validate_sequence rejects every tuple of IOs, so sequence must fail with the friendly
// static_assert message containing the custom reason, even though the elements are valid IOs and
// would otherwise sequence. Because traits are no longer inherited, the traits are passed
// explicitly to sequence.

namespace
{
/// Custom traits that accept valid actions but reject every tuple of IOs.
struct RejectSequence
{
    static constexpr std::string_view name = "SequenceIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_sequence(std::meta::info /*tuple_meta*/)
    {
        return "rejected by custom sequence traits";
    }
};
}  // namespace

void sequence_elements()
{
    using namespace fnfelt::literals;  // NOLINT
    namespace io = fnfelt::monad::io;
    auto const sequenced = fnfelt::monad::io::sequence<RejectSequence>(std::make_tuple(
        io::IO<int (*)(), io::SequenceIOTraits<"First"_ss>>{[] { return 1; }},
        io::IO<int (*)(), io::SequenceIOTraits<"Second"_ss>>{[] { return 2; }}));
}
