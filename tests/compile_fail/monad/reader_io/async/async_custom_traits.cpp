// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/reader_io/async.hpp>
#include <fnfelt/monad/reader_io/detail/async.hpp>

// RejectAsync is a custom traits type whose validate_create_async rejects every coroutine/
// argument combination, so create_async must fail with the friendly static_assert message
// containing the custom reason, even though the combination itself is valid. Because the custom
// traits short-circuit before the IO delegation, the fn's shape is never even validated.

namespace
{
/// Valid-shaped coroutine function (never validated by these traits).
struct EchoFn
{
    auto operator()(auto /*self*/, int /*state*/) const -> lf::task<int>;
};

/// Custom traits that accept valid actions but reject every create_async combination.
struct RejectAsync : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "RejectReader";

    static consteval std::string_view validate_create_async(
        std::meta::info /*fn_meta*/, std::meta::info /*arg_tuple_meta*/)
    {
        return "rejected by custom async traits";
    }
};
}  // namespace

void create_async_fn()
{
    auto const reader = fnfelt::monad::reader_io::create_async<int, RejectAsync>(EchoFn{});
    (void)reader;
}
