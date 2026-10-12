// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>

// RejectAsync is a custom traits type whose validate_action accepts the valid async leaf action,
// but whose validate_create_async rejects every function/argument pair, so create_async must fail
// with the friendly static_assert message containing the custom reason and name, even though the
// async function itself is otherwise valid.

namespace
{
/// Custom traits that accept valid actions but reject every async function/argument pair.
struct RejectAsync
{
    static constexpr std::string_view name = "CustomAsyncIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_create_async(
        std::meta::info /*fn_meta*/, std::meta::info /*arg_tuple_meta*/)
    {
        return "rejected by custom async traits";
    }
};
}  // namespace

void create_async()
{
    auto const async_io = fnfelt::monad::io::create_async<RejectAsync>(
        [](auto, int x) -> lf::task<int> { co_return x * 2; }, 21);
    (void)async_io;
}
