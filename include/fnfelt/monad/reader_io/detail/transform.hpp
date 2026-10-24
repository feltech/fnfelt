// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../reader_io.hpp"
/**
 * @file monad/reader_io/detail/transform.hpp
 *
 * Implementation details for the ReaderIO monad's functor mapping.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <utility>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/type_names.hpp>
// The action delegates the mapping to the inner IO's transform, whose definition (and the IO
// member's deduction target) is required here.
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/transform.hpp>
// The inner IO's transform validator (`TransformIOTraits::validate_transform`).
#include <fnfelt/monad/io/detail/transform.hpp>
#include <fnfelt/monad/reader_io/detail.hpp>
#include <fnfelt/monad/reader_io/detail/create.hpp>
#include <fnfelt/monad/reader_io/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::reader_io
{
namespace detail
{

/**
 * Tombstone returned by `reader_io::transform` when the transformer is invalid.
 *
 * The invalid transform is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct TransformError
{
};

/**
 * Constructs the diagnostic message for a rejected composition.
 *
 * The message includes the ReaderIO name, the composition signature
 * `<name>{<source name>() => <transformer short name>}`, the specific validation failure reason,
 * and the offending transformer's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_meta Reflection of the source ReaderIO type.
 * @tparam transformer_meta Reflection of the (decayed) transformer type.
 * @param name Display name of the resulting ReaderIO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_transform`.
 * @return Formatted error message string.
 */
template <std::meta::info source_meta, std::meta::info transformer_meta>
consteval std::string transform_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += " transform error: ";
    fnfelt::detail::append_string_view(msg, name);
    msg += '{';
    fnfelt::detail::append_string_view(msg, maybe_reader_io_name<source_meta>());
    msg += "() => ";
    fnfelt::detail::append_string_view(
        msg, fnfelt::detail::short_type_name(dealias(transformer_meta)));
    msg += "}: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(transformer_meta));
    return msg;
}

/**
 * Action that invokes a source ReaderIO with the state and transforms the produced value, keeping
 * the transformer's plain result.
 *
 * No state copy: the state is forwarded to the source unchanged and never reaches the transformer,
 * whose result is a plain value. The mapping is delegated to the inner IO's transform, so the
 * transformer follows the IO transform contract (bare-type validation done at composition time;
 * the member takes the transformer by value with the composition's value category, so it is not
 * moved out on an lvalue run) and replays work.
 *
 * @tparam TState The ReaderIO's state type.
 * @tparam TSource Source ReaderIO to invoke.
 * @tparam TTransformer Transformer taking the source's produced value and returning a plain value.
 */
template <class TState, class TSource, class TTransformer>
struct TransformAction
{
    /// Source ReaderIO to invoke.
    TSource source;
    /// Transformer applied to the source's value.
    TTransformer transformer;

    /**
     * Invoke the source with the state and transform its produced value.
     *
     * @param self The action to run (explicit object parameter).
     * @param state The environment for this invocation.
     * @return The transformed IO.
     */
    constexpr auto operator()(this auto && self, TState state)
    {
        return FW(self).source.build(std::move(state)).io().transform(FW(self).transformer);
    }
};

/**
 * Traits validating the source ReaderIO and transformer function provided to
 * `reader_io::transform`.
 *
 * @tparam name_cstr Null-terminated name of the ReaderIO instantiation, used in diagnostics. Must
 * have static storage duration.
 */
template <char const * name_cstr>
struct TransformReaderIOTraits : CreateReaderIOTraits<name_cstr>
{
    /**
     * Validate the source ReaderIO and transformer function.
     *
     * The source must be a ReaderIO whose action produces a value; the transformer must have the
     * stored-callable shape shared with the IO monad. The value-dependent transformer contract
     * (accepting the source's produced value directly or spread, and returning a plain value) is
     * delegated to the inner IO's transform validator on the reflected inner IO.
     *
     * @param source_meta Reflection of the source ReaderIO type.
     * @param transformer_meta Reflection of the (decayed) transformer type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_transform(
        std::meta::info source_meta, std::meta::info transformer_meta)
    {
        if (!detail::is_reader_io(source_meta))
        {
            return "source is not a ReaderIO";
        }
        std::meta::info const inner_io_meta = detail::reader_io_io_meta(source_meta);
        if (is_void_type(inner_io_meta))
        {
            return "source ReaderIO produces no value";
        }
        switch (fnfelt::detail::callable_shape_of(transformer_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "transformer function is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "transformer function must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "transformer function is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "transformer function must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        return io::detail::TransformIOTraits<>::validate_transform(
            inner_io_meta, dealias(remove_cvref(transformer_meta)));
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::reader_io

#include <fnfelt/macros_pop.hpp>
