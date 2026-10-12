// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file detail/transform.hpp
 *
 * Implementation-detail helpers for the `io::transform` operations.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
// Forward declaration for the detail helpers below, which reflect on the proxy. The proxy itself
// is defined alongside the free functions.
template <class TSourceIO, class TTransformer>
struct TransformProxy;

namespace detail
{

/**
 * Tombstone returned by `io::transform` when the transformer is invalid.
 *
 * The invalid transform is already rejected by a friendly static_assert; this type exists only to
 * keep the return type well-formed and must never be used.
 */
struct TransformError
{
};

/**
 * Reflection of the value a transform action produces when run.
 *
 * The transformer is applied with the source's value directly when possible, otherwise the value is
 * spread into it (direct invocation wins over spreading), mirroring the action's dispatch. The
 * value is unwrapped through async proxies, so this works for both sync and async IOs.
 *
 * Computes the synchronous composition's result, which `transform_result_type_meta` delegates to on
 * the sync path and which types the async proxy's value. Deriving those declared types from it
 * keeps validating the action from instantiating its body, which would otherwise require copying a
 * move-only nested action for the const receiver.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @return Reflection of the value the action produces.
 */
template <class TSourceIO, class TTransformer>
consteval std::meta::info transform_result_meta()
{
    constexpr std::meta::info value_meta = io_value_meta(^^TSourceIO);
    if constexpr (is_directly_invocable(^^TTransformer, value_meta))
    {
        return invoke_result(^^TTransformer, {value_meta});
    }
    if constexpr (is_spread_invocable(^^TTransformer, value_meta))
    {
        return invoke_result(^^TTransformer, template_arguments_of(dealias(value_meta)));
    }
    return ^^void;
}

/**
 * Whether a transform composition must run asynchronously.
 *
 * The composition is asynchronous when the source IO's action is asynchronous. A transformer
 * returns a plain value, never an IO, so only the source can make the composition asynchronous. The
 * result type and the action's run-time dispatch both derive from this predicate, keeping them in
 * sync.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @return True if the composition runs asynchronously.
 */
template <class TSourceIO, class TTransformer>
consteval bool transform_is_async()
{
    return action_is_async(io_action_meta(^^TSourceIO));
}

/**
 * Reflection of the value a TransformAction's declared return type names.
 *
 * Synchronous compositions produce a plain value; asynchronous ones produce the composed async
 * proxy.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @return Reflection of the action's declared return type.
 */
template <class TSourceIO, class TTransformer>
consteval std::meta::info transform_result_type_meta()
{
    if constexpr (transform_is_async<TSourceIO, TTransformer>())
    {
        return ^^TransformProxy<TSourceIO, TTransformer>;
    }
    return transform_result_meta<TSourceIO, TTransformer>();
}

/**
 * Action that runs a source IO and feeds its value to a transformer, keeping the plain result.
 *
 * Synchronous compositions run the source inline and produce a plain value; asynchronous ones
 * instead materialise the composed async proxy so that the whole tree runs under a single top-level
 * scheduler. The transformer is invoked with the value directly when possible, otherwise the value
 * is spread into the transformer as if by `std::apply` (direct invocation wins over spreading),
 * exactly like `AndThenAction`. Unlike `AndThenAction`, the transformer's result is returned
 * directly rather than run as an IO. The action's value category is forwarded to the source, and
 * the stored transformer is invoked as an rvalue, matching validation and result inference.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 */
template <class TSourceIO, class TTransformer>
struct TransformAction
{
    /// Value the action produces when run (async proxy or plain value).
    using result_type = [:transform_result_type_meta<TSourceIO, TTransformer>():];
    /// Source IO to run.
    TSourceIO source;
    /// Transformer applied to the source's value.
    TTransformer transformer;

    /**
     * Run the source IO, apply the transformer to its value, and return the transformer's result.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The composed async proxy, or the transformer's plain result on the synchronous path.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        if constexpr (transform_is_async<TSourceIO, TTransformer>())
        {
            return result_type{
                std::tuple<TSourceIO, TTransformer>{FW(self).source, FW(self).transformer}};
        }
        if constexpr (!transform_is_async<TSourceIO, TTransformer>())
        {
            using source_value_type = TSourceIO::value_type;
            auto input = FW(self).source.build().sync_wait();
            if constexpr (is_directly_invocable(^^TTransformer, ^^source_value_type))
            {
                return FW(self).transformer(std::move(input));
            }
            if constexpr (is_spread_invocable(^^TTransformer, ^^source_value_type))
            {
                return std::apply(FW(self).transformer, std::move(input));
            }
        }
        // Not reachable: validation guarantees one of the dispatches above is taken.
        std::unreachable();
    }
};

/**
 * Name of the value produced by a transform application's result.
 *
 * An IO result shows the IO's name instead of its kind-based short name; any other result shows its
 * kind-based short name (see `detail::short_type_name`).
 *
 * @tparam result_meta Reflection of the transformer's result.
 * @return Short name of the transformer's result.
 */
template <std::meta::info result_meta>
constexpr std::string_view transform_result_name()
{
    if constexpr (is_io(result_meta))
    {
        return maybe_io_name<result_meta>();
    }
    return short_type_name(result_meta);
}

/**
 * Name of the value produced by applying a transformer to a source IO's value.
 *
 * The application is dispatched like `TransformAction`: direct invocation when possible, otherwise
 * spreading the value's template arguments. Its result is named by `transform_result_name` (an IO
 * result is unusual for transform); any part not determinable from the reflections yields
 * "<unknown>".
 *
 * @tparam source_io_meta Reflection of the source IO.
 * @tparam transformer_meta Reflection of the transformer.
 * @return Short name of the transformer's result if determinable, otherwise "<unknown>".
 */
template <std::meta::info source_io_meta, std::meta::info transformer_meta>
constexpr std::string_view maybe_transformer_result_name()
{
    if constexpr (is_io(source_io_meta) && is_complete_type(transformer_meta))
    {
        constexpr std::meta::info source_value_meta = io_value_meta(source_io_meta);
        if constexpr (is_invocable_type(transformer_meta, {source_value_meta}))
        {
            return transform_result_name<invoke_result(transformer_meta, {source_value_meta})>();
        }
        if constexpr (is_spread_invocable(transformer_meta, source_value_meta))
        {
            return transform_result_name<invoke_result(
                transformer_meta, template_arguments_of(dealias(source_value_meta)))>();
        }
    }

    return "<unknown>";
}

/**
 * Constructs a detailed error message for an invalid `io::transform` operation.
 *
 * Uses reflection to extract type names and diagnostics about the source IO and transformer when
 * transform validation fails. The message includes the IO name, the source IO type, the
 * transformer's result type (if determinable), the specific validation failure reason, and the
 * offending argument's display string (the source type if the source is not an IO, otherwise the
 * transformer type).
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_io_meta Reflection info for the source IO type.
 * @tparam transformer_meta Reflection info for the transformer type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_transform`.
 * @return Formatted error message string.
 */
template <std::meta::info source_io_meta, std::meta::info transformer_meta>
consteval std::string transform_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO transform error: ";
    append_string_view(msg, io_name);
    msg += "{(";
    append_string_view(msg, maybe_io_name<source_io_meta>());
    msg += "()) => ";
    append_string_view(msg, maybe_transformer_result_name<source_io_meta, transformer_meta>());
    msg += "}: ";
    append_string_view(msg, reason);
    msg += ": ";
    std::meta::info const target_meta =
        !detail::is_io(source_io_meta) ? source_io_meta : transformer_meta;
    append_string_view(msg, display_string_of(target_meta));

    return msg;
}
}  // namespace detail

/**
 * Traits validating the source IO and transformer function provided to `io::transform`.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct TransformIOTraits : CreateIOTraits<name_cstr>
{
    /**
     * Validate the source IO and transformer function provided to @ref transform.
     *
     * Checks, in order: the source is an IO producing a value (not void); the transformer function
     * is a complete, non-reference, move constructible class or function pointer type; it accepts
     * the source IO's value either directly or spread from its template arguments; and its result
     * is a valid plain value (not void, not a reference, not an IO, and move constructible).
     *
     * This is the overridable default implementation used by `io::transform`: custom traits may
     * replace it to change which source IO and transformer pairs are accepted.
     *
     * @param io_meta Reflection of the source IO type.
     * @param transformer_meta Reflection of the transformer function type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_transform(
        std::meta::info io_meta, std::meta::info transformer_meta)
    {
        if (!detail::is_io(io_meta))
        {
            return "source IO is not an IO";
        }
        auto const value_meta = detail::io_value_meta(io_meta);
        if (is_void_type(value_meta))
        {
            return "source IO produces no value";
        }
        if (!is_complete_type(transformer_meta))
        {
            return "transformer function is not a complete type";
        }
        if (is_reference_type(transformer_meta))
        {
            return "transformer function must not be a reference type";
        }
        transformer_meta = remove_cvref(transformer_meta);
        if (!is_class_type(transformer_meta) &&
            !(is_pointer_type(transformer_meta) &&
              is_function_type(remove_pointer(transformer_meta))))
        {
            return "transformer function is not a class or function pointer type";
        }
        if (!is_move_constructible_type(transformer_meta))
        {
            return "transformer function must be move constructible";
        }
        auto const validate_result = [](std::meta::info result_meta) consteval -> std::string_view
        {
            if (is_void_type(result_meta))
            {
                return "transformer function does not return a value";
            }
            if (is_reference_type(result_meta))
            {
                return "transformer function returns a reference, return by value instead";
            }
            if (detail::is_io(result_meta))
            {
                return "transformer function should not return an IO";
            }
            if (!is_move_constructible_type(result_meta))
            {
                return "transformer function returns a non-movable type";
            }
            return {};
        };
        if (detail::is_directly_invocable(transformer_meta, value_meta))
        {
            return validate_result(invoke_result(transformer_meta, {value_meta}));
        }
        if (detail::is_spread_invocable(transformer_meta, value_meta))
        {
            return validate_result(
                invoke_result(transformer_meta, template_arguments_of(dealias(value_meta))));
        }
        return "transformer function does not accept the source IO's value";
    }
};
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
