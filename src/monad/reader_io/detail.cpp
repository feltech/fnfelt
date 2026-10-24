// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/reader_io/detail.hpp>

#include <doctest/doctest.h>

#include <type_traits>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/reader_io/ReaderIO.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>

namespace
{
/// Custom ReaderIO name for a custom-traits instantiation.
inline constexpr char custom_reader_io_name[] = "CustomReader";
using CustomReaderIOTraits =
    fnfelt::monad::reader_io::detail::CreateReaderIOTraits<custom_reader_io_name>;
}  // namespace

TEST_CASE("detail::is_reader_io recognises ReaderIO specialisations with any traits")
{
    using fnfelt::monad::reader_io::detail::is_reader_io;

    static_assert(is_reader_io(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)>));
    static_assert(is_reader_io(
        ^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int), CustomReaderIOTraits>));
    // Cv-ref-qualified reflections strip to the specialisation.
    static_assert(is_reader_io(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)> &));
    static_assert(is_reader_io(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)> const &&));
    static_assert(!is_reader_io(^^int));
    // The IO monad's type is not a ReaderIO.
    static_assert(!is_reader_io(^^fnfelt::monad::io::IO<int (*)()>));
}

TEST_CASE("detail::reader_io_state_meta reflects a ReaderIO's state type (index 0)")
{
    using fnfelt::monad::reader_io::detail::reader_io_state_meta;

    static_assert(is_same_type(
        reader_io_state_meta(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)>), ^^int));
    // NOLINTBEGIN(runtime/int)
    static_assert(is_same_type(
        reader_io_state_meta(^^fnfelt::monad::reader_io::ReaderIO<long, double (*)(long)>),
        ^^long));
    // NOLINTEND(runtime/int)
    // Cv-ref-qualified reflections strip to the specialisation.
    static_assert(is_same_type(
        reader_io_state_meta(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)> &), ^^int));
    // Non-ReaderIO types have no state.
    static_assert(is_same_type(reader_io_state_meta(^^int), ^^void));
}

TEST_CASE("detail::reader_io_action_meta reflects a ReaderIO's action type (index 1)")
{
    using fnfelt::monad::reader_io::detail::reader_io_action_meta;

    static_assert(is_same_type(
        reader_io_action_meta(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)>),
        ^^int (*)(int)));
    // NOLINTBEGIN(runtime/int)
    static_assert(is_same_type(
        reader_io_action_meta(^^fnfelt::monad::reader_io::ReaderIO<long, double (*)(long)>),
        ^^double (*)(long)));
    // NOLINTEND(runtime/int)
    static_assert(is_same_type(
        reader_io_action_meta(^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)> &),
        ^^int (*)(int)));
    // Non-ReaderIO types have no wrapped action.
    static_assert(is_same_type(reader_io_action_meta(^^int), ^^void));
    static_assert(is_same_type(reader_io_action_meta(^^fnfelt::monad::io::IO<int (*)()>), ^^void));
}

TEST_CASE("detail::maybe_reader_io_name names ReaderIOs and falls back for non-ReaderIOs")
{
    using fnfelt::monad::reader_io::detail::maybe_reader_io_name;

    static_assert(
        maybe_reader_io_name<^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int)>>() ==
        "ReaderIO");
    static_assert(
        maybe_reader_io_name<
            ^^fnfelt::monad::reader_io::ReaderIO<int, int (*)(int), CustomReaderIOTraits>>() ==
        "CustomReader");
    static_assert(maybe_reader_io_name<^^int>() == "<unknown>");
    static_assert(maybe_reader_io_name<^^fnfelt::monad::io::IO<int (*)()>>() == "<unknown>");
}

#endif  // DOCTEST_CONFIG_DISABLE
