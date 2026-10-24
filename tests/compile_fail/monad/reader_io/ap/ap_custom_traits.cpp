// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/ap.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/ap.hpp>

// RejectAp is a custom traits type whose validate_ap rejects every source pair, so ap must fail
// with the friendly static_assert message containing the custom reason, even though the pair
// itself is valid.

namespace
{
/// Function source action producing a callable.
struct FnSourceAction
{
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(2);
    }
};

/// Value source action producing a value.
struct ValueSourceAction
{
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(1);
    }
};

/// Custom traits that accept valid actions but reject every ap pair.
struct RejectAp : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "RejectReader";

    static consteval std::string_view validate_ap(
        std::meta::info /*fn_meta*/, std::meta::info /*value_meta*/)
    {
        return "rejected by custom ap traits";
    }
};
}  // namespace

void apply()
{
    auto const applied = fnfelt::monad::reader_io::ap<RejectAp>(
        fnfelt::monad::reader_io::create<int>(FnSourceAction{}),
        fnfelt::monad::reader_io::create<int>(ValueSourceAction{}));
    (void)applied;
}
