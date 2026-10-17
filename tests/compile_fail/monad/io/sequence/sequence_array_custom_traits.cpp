// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <meta>

#include <string_view>

#include <immer/array.hpp>

#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/sequence.hpp>

namespace
{
/// Stub rejecting an otherwise valid array to exercise the sequence assertion.
struct RejectArray : fnfelt::monad::io::detail::SequenceIOTraits<>
{
    static consteval std::string_view validate_sequence(std::meta::info /*array_meta*/)
    {
        return "rejected by custom array sequence traits";
    }
};
}  // namespace

void sequence_array()
{
    namespace io = fnfelt::monad::io;
    auto const sequenced = io::sequence<RejectArray>(immer::array{io::just(1)});
}
