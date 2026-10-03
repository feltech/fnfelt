// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/traits.hpp>
#include <fnfelt/static_string.hpp>

// RejectAp is a custom traits type whose validate_action accepts the valid actions, but whose
// validate_ap rejects every function/value IO pair, so ap must fail with the friendly static_assert
// message containing the custom reason, even though both IOs construct and would otherwise be a
// valid ap pair. Because traits are no longer inherited, the traits are passed explicitly to ap.

namespace
{
/// Custom traits that accept valid actions but reject every ap function/value IO pair.
struct RejectAp
{
    static constexpr std::string_view name = "ApIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::IOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_ap(
        std::meta::info /*fn_io_meta*/, std::meta::info /*value_io_meta*/)
    {
        return "rejected by custom ap traits";
    }
};

/// Action returning a callable taking an int - a valid ap function IO action.
struct FnAction
{
    auto operator()() const
    {
        return [](int x) { return x + 1; };
    }
};
}  // namespace

void ap_function_io()
{
    using namespace fnfelt::literals;  // NOLINT
    namespace io = fnfelt::monad::io;
    io::IO<FnAction, io::IOTraits<"FnIO"_ss>> const fn_io{FnAction{}};
    io::IO<int (*)(), io::IOTraits<"ValueIO"_ss>> const value_io{[] { return 1; }};
    auto const applied = fnfelt::monad::io::ap<RejectAp>(fn_io, value_io);
}
