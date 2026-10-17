// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/sequence.hpp
 *
 * Implementation-detail helpers for the `io::sequence` operation.
 *
 * Holds the fold over `ap` that collects the element values, the action that runs it, and the
 * traits validating the tuple of IOs.
 */
#pragma once

#include <meta>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/ap.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>
#include <fnfelt/monad/io/just.hpp>
#include <fnfelt/monad/io/transform.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/**
 * Whether a reflection names a specialisation of `std::tuple`.
 *
 * @param type_meta Reflection of the candidate type.
 * @return True if the reflection names a `std::tuple` specialisation.
 */
consteval bool is_sequence_tuple(std::meta::info type_meta)
{
    // `remove_cvref` and `template_of` throw for a non-type reflection, so inspect the kind first.
    if (!is_type(type_meta))
    {
        return false;
    }
    // Alias reflections report `has_template_arguments` false, so dealias before inspecting the
    // specialisation.
    type_meta = dealias(remove_cvref(type_meta));
    return has_template_arguments(type_meta) && template_of(type_meta) == ^^std::tuple;
}

/**
 * Appends one value to a stored tuple as a single element.
 *
 * The value is appended whole, so a value that is itself a `std::pair` or `std::tuple` becomes one
 * element rather than being flattened; each element of a sequence therefore contributes exactly one
 * element to the collected tuple.
 *
 * @tparam TTuple Stored tuple the value is appended to.
 */
template <class TTuple>
struct TupleAppender
{
    /// Tuple the value is appended to.
    TTuple vals;

    /**
     * Append the value to the stored tuple.
     *
     * @param self The appender holding the stored tuple (explicit object parameter).
     * @param value Value to append as a single element.
     * @return The stored tuple with the value appended.
     */
    constexpr auto operator()(this auto && self, auto && value)
    {
        return std::tuple_cat(FW(self).vals, std::tuple<std::decay_t<decltype(value)>>{FW(value)});
    }
};

/**
 * Tombstone returned by `io::sequence` when the tuple of IOs is invalid.
 *
 * The invalid sequence is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct SequenceError
{
};

/**
 * Whether a sequence of IOs must run asynchronously.
 *
 * The sequence is asynchronous when any element IO's action is asynchronous, since every element is
 * composed through `ap`. The action's run-time dispatch derives from this predicate, keeping the
 * two in sync.
 *
 * @tparam TIOs Element IO types.
 * @return True if the sequence runs asynchronously.
 */
template <class... TIOs>
consteval bool sequence_is_async()
{
    return (action_is_async(io_action_meta(^^TIOs)) || ...);
}

/**
 * Fold one element of a sequence into the accumulated IO.
 *
 * Each step lifts the accumulator's tuple into an appender and applies it to the element's value
 * through `ap`, so the step inherits `ap`'s concurrency and the fold stays a single IO tree. The
 * tuple is taken by value so every step may move the element it consumes.
 *
 * @tparam idx Element index to fold.
 * @tparam TTuple Element tuple type.
 * @tparam TAcc Accumulated IO type.
 * @param ios Element tuple to fold.
 * @param acc Accumulated IO.
 * @return IO producing the tuple of the values of the elements from @p idx onwards.
 */
template <std::size_t idx, class TTuple, class TAcc>
constexpr auto sequence_fold_step(TTuple && ios, TAcc acc)
{
    if constexpr (idx == std::tuple_size_v<std::remove_cvref_t<TTuple>>)
    {
        return acc;
    }
    if constexpr (idx < std::tuple_size_v<std::remove_cvref_t<TTuple>>)
    {
        // The appender takes the accumulator whole: constraining it to tuples keeps `transform`'s
        // spread dispatch from matching, which would spread the accumulator's elements into the
        // appender instead of appending them as one element.
        auto const appender_factory = [](auto && values)
                                          requires(is_sequence_tuple(^^decltype(values)))
        { return TupleAppender<std::decay_t<decltype(values)>>{FW(values)}; };
        // cpplint mistakes the free `transform` for std::transform; no <algorithm> is used here.
        // NOLINTNEXTLINE(build/include_what_you_use)
        auto next = ap(transform(std::move(acc), appender_factory), std::get<idx>(std::move(ios)));
        return sequence_fold_step<idx + 1>(std::move(ios), std::move(next));
    }
}

/**
 * Fold a tuple of IOs left over `ap` into a single IO of their collected values.
 *
 * The seed is an IO of the empty tuple, and each element appends its value as one element, so the
 * result's value is a tuple with one element per input IO, in the input order.
 *
 * @tparam TIOs Element IO types.
 * @param ios Element tuple to run.
 * @return IO producing the tuple of the elements' values.
 */
template <class... TIOs>
constexpr auto sequence_fold(std::tuple<TIOs...> ios)
{
    return sequence_fold_step<0>(std::move(ios), just(std::tuple{}));
}

/**
 * Action that runs a tuple of IOs and collects their values into a tuple, in order.
 *
 * The elements are folded left over `ap`, so an asynchronous element makes the whole fold
 * asynchronous and the elements overlap on the pool. Synchronous sequences run the fold inline and
 * produce the collected tuple; asynchronous ones materialise the fold's composed async proxy so
 * that the whole tree runs under a single top-level scheduler.
 *
 * The action's value category is forwarded to the stored tuple, so running an rvalue moves the
 * element IOs.
 *
 * @tparam TIOs Element IO types.
 */
template <class... TIOs>
struct SequenceAction
{
    /// Element IOs to run, in the order their values are collected.
    std::tuple<TIOs...> ios;

    /**
     * Run every element IO and collect the values.
     *
     * @param self The action to run (explicit object parameter).
     * @return The composed async proxy, or the collected tuple on the synchronous path.
     */
    constexpr auto operator()(this auto && self)
    {
        auto folded = sequence_fold(FW(self).ios);
        if constexpr (sequence_is_async<TIOs...>())
        {
            return std::move(folded).build().action();
        }
        if constexpr (!sequence_is_async<TIOs...>())
        {
            return std::move(folded).build().sync_wait();
        }
    }
};

/**
 * Constructs a detailed error message for an invalid `io::sequence` operation.
 *
 * The message includes the sequence name, the element list `(<io>, <io>, ...)` with `<unknown>` for
 * elements that are not IOs, the validation reason, and the offending element's display string (the
 * first element that is a reference or not an IO, otherwise the first element, otherwise the tuple
 * itself). Qualifiers are stripped before a reflection is named or spliced: a qualified reflection
 * has no identifier, and a reference cannot be spliced into a type, so naming one is a hard error.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam tuple_meta Reflection of the tuple of element types.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_sequence`.
 * @return Formatted error message string.
 */
template <std::meta::info tuple_meta>
consteval std::string sequence_error_msg(std::string_view io_name, std::string_view reason)
{
    // The workhorse passes its `tuple_type` alias by name; dealias so the diagnostic names the
    // underlying tuple specialisation.
    constexpr std::meta::info target_meta = dealias(remove_cvref(tuple_meta));
    std::meta::info offending = target_meta;
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO sequence error: ";
    append_string_view(msg, io_name);
    msg += "{(";
    // `template_of` and `template_arguments_of` throw for a non-type or non-tuple reflection, so
    // the element list stays empty for those and the tuple itself is the offending type.
    if constexpr (is_sequence_tuple(target_meta))
    {
        std::size_t index = 0;
        bool is_offending_set = false;
        template for (constexpr auto element_meta :
                      std::define_static_array(template_arguments_of(target_meta)))
        {
            if (index != 0)
            {
                msg += ", ";
            }
            constexpr std::meta::info named_meta = remove_cvref(element_meta);
            append_string_view(msg, maybe_io_name<named_meta>());
            if (index == 0)
            {
                // Default to the first element: a tuple whose elements all pass the reference and
                // IO checks is still reported against it.
                offending = named_meta;
            }
            if (!is_offending_set &&
                (is_reference_type(element_meta) || !detail::is_io(element_meta)))
            {
                offending = named_meta;
                is_offending_set = true;
            }
            ++index;
        }
    }
    msg += ")}: ";
    append_string_view(msg, reason);
    msg += ": ";
    append_string_view(msg, display_string_of(remove_cvref(offending)));
    return msg;
}
}  // namespace detail

/**
 * Traits validating the tuple of IOs provided to `io::sequence`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct SequenceIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the tuple of IOs provided to @ref sequence.
     *
     * Checks, in order: the argument is a `std::tuple` specialisation, and every element of it is a
     * non-reference IO that produces a value. An empty tuple is accepted: it sequences to an IO of
     * an empty tuple.
     *
     * This is the overridable default implementation used by `io::sequence`: custom traits may
     * replace it to change which tuples of IOs are accepted.
     *
     * @param tuple_meta Reflection of the tuple of element types.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_sequence(std::meta::info tuple_meta)
    {
        // Checked before normalising because `remove_cvref` throws for a non-type reflection.
        if (!is_type(tuple_meta))
        {
            return "provided tuple is not a type";
        }
        tuple_meta = dealias(remove_cvref(tuple_meta));
        if (!detail::is_sequence_tuple(tuple_meta))
        {
            return "provided tuple is not a std::tuple";
        }
        for (std::meta::info const element_meta : template_arguments_of(tuple_meta))
        {
            // Checked before the IO check because `is_io` sees through references, so a reference
            // element would otherwise pass validation and be stored by the sequence's action, which
            // cannot hold a reference.
            if (is_reference_type(element_meta))
            {
                return "element of the tuple must not be a reference type";
            }
            if (!detail::is_io(element_meta))
            {
                return "element of the tuple is not an IO";
            }
            // An element with no value has nothing to collect: the collected tuple would hold a
            // void element.
            if (is_void_type(detail::io_value_meta(element_meta)))
            {
                return "element of the tuple produces no value";
            }
        }
        return {};
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
