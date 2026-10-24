// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>

// RejectCreate is a custom traits type whose validate_action rejects every action/state pair, so
// create must fail with the friendly static_assert message containing the custom reason, even
// though the pair itself is valid. Because traits are passed explicitly, the traits' name is used
// rather than the default "ReaderIO".

namespace
{
/// Environment type.
struct State
{
};

/// Valid state -> IO action.
struct IntAction
{
    auto operator()(State const & /*state*/) const
    {
        return fnfelt::monad::io::just(1);
    }
};

/// Custom traits that reject every action/state pair.
struct RejectCreate : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "CreateIO";

    static consteval std::string_view validate_action(
        std::meta::info /*action_meta*/, std::meta::info /*state_meta*/)
    {
        return "rejected by custom action traits";
    }
};
}  // namespace

void construct()
{
    auto const reader = fnfelt::monad::reader_io::create<State, RejectCreate>(IntAction{});
    (void)reader;
}
