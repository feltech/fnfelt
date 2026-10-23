// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>
#include <tuple>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/traverse.hpp>
#include <fnfelt/static_string.hpp>

// RejectTraverse is a custom traits type whose validate_action accepts the valid actions, but whose
// validate_traverse rejects every tuple and continuation pair, so traverse must fail with the
// friendly static_assert message containing the custom reason, even though the elements are valid
// values (IOs are values here too) and the continuation maps each of them to a value-producing IO
// and would otherwise traverse. validate_traverse is the extension point traverse consults, and
// validate_sequence and validate_action are also consulted because the accepted branch composes
// sequence<TTraits>, so the traits are passed explicitly to traverse.

namespace
{
/// Custom traits that accept valid actions but reject every tuple and continuation pair.
struct RejectTraverse
{
    static constexpr std::string_view name = "TraverseIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        return fnfelt::monad::io::detail::SequenceIOTraits<>::validate_sequence(tuple_meta);
    }

    static consteval std::string_view validate_traverse(
        std::meta::info /*tuple_meta*/, std::meta::info /*continuation_meta*/)
    {
        return "rejected by custom traverse traits";
    }
};
}  // namespace

void traverse_rejected_by_custom_traits()
{
    using namespace fnfelt::literals;  // NOLINT
    namespace io = fnfelt::monad::io;
    auto const traversed = io::traverse<RejectTraverse>(
        std::make_tuple(
            io::IO<int (*)(), io::detail::TraverseIOTraits<"First"_ss>>{[] { return 1; }},
            io::IO<int (*)(), io::detail::TraverseIOTraits<"Second"_ss>>{[] { return 2; }}),
        [](int value) { return io::just(value); });
    (void)traversed;
}
