// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/reader_io/and_then.hpp>
#include <fnfelt/monad/reader_io/create.hpp>
#include <fnfelt/monad/reader_io/detail/and_then.hpp>

// RejectAndThen is a custom traits type whose validate_and_then rejects every source/continuation
// pair, so and_then must fail with the friendly static_assert message containing the custom
// reason, even though the pair itself is valid.

namespace
{
/// Valid int -> IO action.
struct IntAction
{
    auto operator()(int const & /*state*/) const
    {
        return fnfelt::monad::io::just(1);
    }
};

/// Continuation taking the produced value and returning a ReaderIO. Declared only.
struct ContFn
{
    fnfelt::monad::reader_io::ReaderIO<
        int,
        fnfelt::monad::reader_io::detail::
            LiftAction<int, fnfelt::monad::io::IO<fnfelt::monad::io::detail::JustAction<int>>>>
    operator()(int const & /*value*/) const;
};

/// Custom traits that accept valid actions but reject every and_then pair.
struct RejectAndThen : fnfelt::monad::reader_io::detail::CreateReaderIOTraits<>
{
    // The traits' name is overriden so the diagnostic names this instantiation.
    static constexpr std::string_view name = "RejectReader";

    static consteval std::string_view validate_and_then(
        std::meta::info /*source_meta*/, std::meta::info /*continuation_meta*/)
    {
        return "rejected by custom and_then traits";
    }
};
}  // namespace

void chain()
{
    auto const chained = fnfelt::monad::reader_io::and_then<RejectAndThen>(
        fnfelt::monad::reader_io::create<int>(IntAction{}), ContFn{});
    (void)chained;
}
