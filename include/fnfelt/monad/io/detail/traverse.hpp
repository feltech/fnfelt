// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/traverse.hpp
 *
 * Implementation details for applicative traversal of collections.
 */
#pragma once

#include <meta>

#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <immer/array.hpp>
#include <immer/array_transient.hpp>

#include <fnfelt/detail/invocability.hpp>
#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/sequence.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/**
 * Validate immutable input storage before consuming its elements.
 *
 * @param array_meta Immutable array type.
 * @return Empty on success, otherwise a rejection reason.
 */
consteval std::string_view validate_traverse_array(std::meta::info array_meta)
{
    auto const element_meta = template_arguments_of(dealias(remove_cvref(array_meta)))[0];
    if (!is_copy_constructible_type(element_meta))
    {
        return "array element must be copy constructible";
    }
    if (is_trivially_copyable_type(element_meta) && !is_copy_assignable_type(element_meta))
    {
        return "trivially copyable array elements must be copy assignable for Immer storage";
    }
    return {};
}

/**
 * Map immutable array elements through a mutable continuation, preserving the storage policy.
 *
 * @tparam TValue Input value type.
 * @tparam TMemoryPolicy Immutable storage policy.
 * @tparam TContinuation Continuation type.
 * @param values Owned input handle.
 * @param continuation Continuation applied once per element.
 * @return Immutable array of decayed continuation results.
 */
template <class TValue, class TMemoryPolicy, class TContinuation>
constexpr auto map_traverse_array(
    immer::array<TValue, TMemoryPolicy> values, TContinuation & continuation)
{
    using mapped_type = std::decay_t<decltype(fnfelt::detail::invoke_dispatch(
        continuation, std::declval<TValue>()))>;
    auto elements = std::move(values).transient();
    auto children = std::span{elements.data_mut(), elements.size()};
    std::vector<mapped_type> mapped;
    mapped.reserve(children.size());
    for (auto & value : children)
    {
        mapped.push_back(fnfelt::detail::invoke_dispatch(continuation, std::move(value)));
    }
    return immer::array<mapped_type, TMemoryPolicy>{
        std::make_move_iterator(mapped.begin()), std::make_move_iterator(mapped.end())};
}

/**
 * Tombstone returned by `io::traverse` when the tuple of values or the continuation is invalid.
 *
 * The invalid traverse is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct TraverseError
{
};

/**
 * Reflection of the first collection element, stripped of references.
 *
 * @param tuple_meta Reflection of the candidate collection.
 * @return Reflection of the array element type or first tuple element, or `void` if the reflection
 * is unsupported or the tuple is empty. The `void` is the diagnostic placeholder described on
 * `action_value_meta`.
 */
consteval std::meta::info traverse_first_element_meta(std::meta::info tuple_meta)
{
    if (is_sequence_array(tuple_meta))
    {
        return remove_cvref(template_arguments_of(dealias(remove_cvref(tuple_meta)))[0]);
    }
    if (!is_sequence_tuple(tuple_meta))
    {
        return ^^void;
    }
    auto const elements = template_arguments_of(dealias(remove_cvref(tuple_meta)));
    if (elements.empty())
    {
        return ^^void;
    }
    return remove_cvref(elements[0]);
}

/**
 * Name of a tuple element for the diagnostic element list: an IO's traits name, otherwise the
 * reflected type's short name.
 *
 * @return Name of the element type, never empty.
 */
template <std::meta::info element_meta>
consteval std::string_view maybe_element_name()
{
    if constexpr (is_io(element_meta))
    {
        return maybe_io_name<element_meta>();
    }
    return fnfelt::detail::short_type_name(element_meta);
}

/**
 * Name of the IO the continuation returns for a tuple element's value.
 *
 * Mirrors validation: direct invocation wins over spreading, and the continuation is invoked as an
 * lvalue.
 *
 * @tparam value_meta Reflection of the element's value type.
 * @tparam continuation_meta Reflection of the continuation function type.
 * @return The result IO's name, or "<unknown>" when the value or continuation is not a type, the
 * continuation is incomplete, or the continuation does not accept the value. A reference result IO
 * is named through its cvref-stripped type.
 */
template <std::meta::info value_meta, std::meta::info continuation_meta>
consteval std::string_view maybe_traverse_result_io_name()
{
    // The diagnostic that names a result IO is instantiated even when validation fails, and it
    // must never fail to evaluate: `is_invocable_type` on an incomplete type is a hard,
    // non-catchable gcc error, so the completeness half of `is_continuation_shape_valid` is
    // mirrored here - an incomplete (or reference-to-incomplete) continuation still yields
    // "<unknown>". Keep the two in step.
    if constexpr (
        is_type(value_meta) && is_type(continuation_meta) &&
        is_complete_type(remove_cvref(continuation_meta)))
    {
        constexpr std::meta::info receiver_meta = add_lvalue_reference(continuation_meta);
        constexpr std::meta::info result_meta =
            fnfelt::detail::dispatch_result_meta(receiver_meta, value_meta);
        if constexpr (is_io(result_meta))
        {
            // A continuation may return an IO reference; a reference cannot be spliced, and the
            // assertion message is instantiated even when validation passes.
            constexpr std::meta::info named_meta = remove_cvref(result_meta);
            return maybe_io_name<named_meta>();
        }
    }
    return "<unknown>";
}

/**
 * Whether a traverse continuation's own shape passes the corresponding `validate_traverse` checks.
 *
 * The continuation must be a type, complete, not a reference, and (after qualifiers are stripped) a
 * class type or a pointer to a function type. This mirrors the shape half of
 * `TraverseIOTraits::validate_traverse`, without the per-element invocation checks.
 *
 * @param continuation_meta Reflection of the continuation function type.
 * @return True if the continuation's shape is valid.
 */
consteval bool is_continuation_shape_valid(std::meta::info continuation_meta)
{
    if (!is_type(continuation_meta) || !is_complete_type(continuation_meta) ||
        is_reference_type(continuation_meta))
    {
        return false;
    }
    auto const named_meta = remove_cvref(continuation_meta);
    return is_class_type(named_meta) ||
        (is_pointer_type(named_meta) && is_function_type(remove_pointer(named_meta)));
}

/**
 * Constructs the diagnostic message for a rejected traversal.
 *
 * The message includes the traverse name, the element list `(<value>, <value>, ...)` named by
 * `maybe_element_name`, the continuation's result IO name (from the first element's value, or
 * `<unknown>` when undeterminable), the validation reason, and the offending argument's display
 * string (the continuation itself when its shape is invalid - not a type, incomplete, a reference,
 * or neither a class nor a function pointer type; otherwise the first element whose value the
 * continuation does not map to a value-producing IO; otherwise the continuation, e.g. when a custom
 * `validate_traverse` rejected the tuple; otherwise the input itself). Unsupported inputs use
 * `<unknown>` instead of an element list.
 * Qualifiers are stripped before a reflection is named or spliced: a qualified reflection has no
 * identifier, and a reference cannot be spliced into a type, so naming one is a hard error.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never fail to evaluate or throw.
 *
 * @tparam tuple_meta Reflection of the tuple of values.
 * @tparam continuation_meta Reflection of the continuation function type.
 * @param io_name Name of the IO instantiation, from its traits.
 * @param reason Human-readable validation failure reason, from its traits.
 * @return Diagnostic string.
 */
template <std::meta::info tuple_meta, std::meta::info continuation_meta>
consteval std::string traverse_error_msg(std::string_view io_name, std::string_view reason)
{
    // Normalise type aliases and qualifiers without querying a non-type reflection as a type.
    constexpr std::meta::info target_meta = []
    {
        if constexpr (is_type(tuple_meta))
        {
            return dealias(remove_cvref(tuple_meta));
        }
        return tuple_meta;
    }();
    std::meta::info offending = target_meta;
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO traverse error: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += "{";
    if constexpr (is_sequence_array(target_meta))
    {
        constexpr auto element_meta = template_arguments_of(target_meta)[0];
        msg += "immer::array<";
        fnfelt::detail::append_string_view(msg, maybe_element_name<element_meta>());
        msg += ">";
        offending = continuation_meta;
        if constexpr (is_continuation_shape_valid(continuation_meta))
        {
            constexpr auto result_meta = fnfelt::detail::dispatch_result_meta(
                add_lvalue_reference(continuation_meta), element_meta);
            if constexpr (!is_io(result_meta) || is_void_type(io_value_meta(result_meta)))
            {
                offending = element_meta;
            }
        }
    }
    else if constexpr (is_sequence_tuple(target_meta))
    {
        msg += "(";
        std::size_t index = 0;
        template for (constexpr auto element_meta :
                      std::define_static_array(template_arguments_of(target_meta)))
        {
            if (index > 0)
            {
                msg += ", ";
            }
            constexpr std::meta::info named_meta = remove_cvref(element_meta);
            fnfelt::detail::append_string_view(msg, maybe_element_name<named_meta>());
            ++index;
        }
        msg += ")";
        // A continuation whose own shape is invalid is the offending argument itself: the scan
        // below is skipped rather than blaming a tuple element a shape-invalid continuation cannot
        // even be asked about.
        bool is_offending_set = false;
        if constexpr (is_continuation_shape_valid(continuation_meta))
        {
            // Scan the elements with the same dispatch validation uses - direct invocation wins
            // over spreading, and the continuation is invoked as an lvalue - to find the first
            // element whose value the continuation does not map to a value-producing IO. Every
            // element accepted means the continuation itself is the offending argument (e.g. a
            // custom validate_traverse rejected the tuple after an all-valid element scan).
            // The shape check above guarantees a type, so add_lvalue_reference cannot fail.
            constexpr std::meta::info receiver_meta = add_lvalue_reference(continuation_meta);
            template for (constexpr auto element_meta :
                          std::define_static_array(template_arguments_of(target_meta)))
            {
                constexpr std::meta::info named_meta = remove_cvref(element_meta);
                if (!is_offending_set)
                {
                    std::meta::info const result_meta =
                        fnfelt::detail::dispatch_result_meta(receiver_meta, element_meta);
                    if (!is_io(result_meta) ||
                        (is_io(result_meta) && is_void_type(io_value_meta(result_meta))))
                    {
                        offending = named_meta;
                        is_offending_set = true;
                    }
                }
            }
        }
        // Unscanned (shape-invalid continuation) or fully-accepted elements leave the offending
        // argument as the continuation itself.
        if (!is_offending_set)
        {
            offending = continuation_meta;
        }
    }
    else
    {
        msg += "<unknown>";
    }
    msg += " => ";
    constexpr std::meta::info first_meta = traverse_first_element_meta(target_meta);
    fnfelt::detail::append_string_view(
        msg, maybe_traverse_result_io_name<first_meta, continuation_meta>());
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(offending));
    return msg;
}

/**
 * Traits validating the collection of values and continuation provided to `io::traverse`.
 *
 * Derives from `SequenceIOTraits` because the operation composes `sequence<TTraits>`, so the
 * consulted extension points are `validate_traverse`, `validate_sequence` and `validate_action`.
 * `validate_traverse` checks the collection, continuation and mapped IOs.
 * Arrays validate the mapped collection through the sequence traits.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct TraverseIOTraits : SequenceIOTraits<name_cstr>
{
    /**
     * Validate the collection of values and continuation function.
     *
     * Checks, in order: the argument is a `std::tuple` or `immer::array`; the continuation is a
     * complete, non-reference class or function pointer type; and, for each element, the
     * continuation - invoked as a non-const lvalue, mirroring the run-time invocation - accepts the
     * element's value directly or spread (direct invocation wins over spreading) and returns an IO
     * that produces a value. There is no copy or move constructibility requirement on the
     * continuation: it is taken by value and invoked as an lvalue, applied once per element, so a
     * move-only continuation is accepted. An empty tuple is accepted and exempt from the
     * per-element check: it never applies the continuation and traverses to an IO of an empty
     * tuple.
     * Array input elements and mapped IOs must support Immer's copy-on-write storage. The
     * element invocation is checked even for empty arrays, whose result type remains homogeneous.
     *
     * @param tuple_meta Reflection of the collection of element value types.
     * @param continuation_meta Reflection of the continuation function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_traverse(
        std::meta::info tuple_meta, std::meta::info continuation_meta)
    {
        if (!is_type(tuple_meta))
        {
            return "provided tuple is not a type";
        }
        tuple_meta = dealias(remove_cvref(tuple_meta));
        if (!detail::is_sequence_tuple(tuple_meta) && !detail::is_sequence_array(tuple_meta))
        {
            return "provided collection is not a std::tuple or immer::array";
        }
        if (!is_type(continuation_meta))
        {
            return "continuation function is not a type";
        }
        if (!is_complete_type(continuation_meta))
        {
            return "continuation function is not a complete type";
        }
        if (is_reference_type(continuation_meta))
        {
            return "continuation function must not be a reference type";
        }
        continuation_meta = remove_cvref(continuation_meta);
        if (!is_class_type(continuation_meta) &&
            !(is_pointer_type(continuation_meta) &&
              is_function_type(remove_pointer(continuation_meta))))
        {
            return "continuation function is not a class or function pointer type";
        }
        // The continuation is held by value in the call and invoked as a non-const lvalue: it is
        // applied to every element in turn when the composition is built and never moved. The
        // lvalue-referenced receiver models that invocation.
        std::meta::info const receiver_meta = add_lvalue_reference(continuation_meta);
        auto elements = template_arguments_of(tuple_meta);
        if (detail::is_sequence_array(tuple_meta))
        {
            if (auto const reason = validate_traverse_array(tuple_meta); !reason.empty())
            {
                return reason;
            }
            elements.resize(1);
        }
        for (auto const element_meta : elements)
        {
            if (!fnfelt::detail::is_dispatch_invocable(receiver_meta, element_meta))
            {
                return "continuation function does not accept the tuple element's value";
            }
            std::meta::info const result_meta =
                fnfelt::detail::dispatch_result_meta(receiver_meta, element_meta);
            if (!detail::is_io(result_meta))
            {
                return "continuation function must return an IO";
            }
            if (is_void_type(detail::io_value_meta(result_meta)))
            {
                return "continuation function must return an IO that produces a value";
            }
            if (detail::is_sequence_array(tuple_meta))
            {
                auto const policy_meta = template_arguments_of(tuple_meta)[1];
                auto const mapped_meta =
                    substitute(^^immer::array, {remove_cvref(result_meta), policy_meta});
                if (auto const reason = validate_traverse_array(mapped_meta); !reason.empty())
                {
                    return reason;
                }
                return SequenceIOTraits<name_cstr>::validate_sequence(mapped_meta);
            }
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io
