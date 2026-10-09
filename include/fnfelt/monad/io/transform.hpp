// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file transform.hpp
 *
 * Free `transform` operation for the IO monad (and its implementation details).
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `transform`
 * functions.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
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
 * Reflection of the value a TransformAction produces when run.
 *
 * The transformer is applied with the source's value directly when possible, otherwise the value
 * is spread into it (direct invocation wins over spreading), mirroring the action's dispatch.
 *
 * Used as the action's declared return type so that validating the action never instantiates its
 * body, which would otherwise require copying a move-only nested action for the const receiver.
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
 * Action that runs a source IO and feeds its value to a transformer, keeping the plain result.
 *
 * The transformer is invoked with the value directly when possible, otherwise the value is spread
 * into the transformer as if by `std::apply` (direct invocation wins over spreading), exactly like
 * `AndThenAction`. Unlike `AndThenAction`, the transformer's result is returned directly rather
 * than run as an IO. The action's value category is forwarded to the source and transformer, so
 * running an rvalue moves captures. The transformer must still be callable as a const lvalue.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 */
template <class TSourceIO, class TTransformer>
struct TransformAction
{
    /// Value the action produces when run.
    using result_type = [:transform_result_meta<TSourceIO, TTransformer>():];
    /// Source IO to run.
    TSourceIO source;
    /// Transformer applied to the source's value.
    TTransformer transformer;

    /**
     * Run the source IO, apply the transformer to its value, and return the transformer's result.
     *
     * @param self The action to run (explicit object parameter).
     *
     * @return The transformer's plain result.
     */
    constexpr auto operator()(this auto && self) -> result_type
    {
        using source_value_type = TSourceIO::value_type;
        auto input = FW(self).source().sync_wait();
        if constexpr (is_directly_invocable(^^TTransformer, ^^source_value_type))
        {
            return FW(self).transformer(std::move(input));
        }
        if constexpr (is_spread_invocable(^^TTransformer, ^^source_value_type))
        {
            return std::apply(FW(self).transformer, std::move(input));
        }
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
        if constexpr (is_spread_invocable_as(transformer_meta, source_value_meta))
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
    msg += io_name;
    msg += "{(";
    msg += maybe_io_name<source_io_meta>();
    msg += "()) => ";
    msg += maybe_transformer_result_name<source_io_meta, transformer_meta>();
    msg += "}: ";
    msg += reason;
    msg += ": ";
    std::meta::info const target_meta =
        !detail::is_io(source_io_meta) ? source_io_meta : transformer_meta;
    msg += display_string_of(target_meta);

    return msg;
}
}  // namespace detail

/**
 * Transforms a source IO's value with a transformer, re-wrapping the plain result in an IO.
 *
 * The returned IO runs the source IO, applies the transformer to its value, and produces the
 * transformer's plain value. Values that are specialisations with all-type template arguments
 * (e.g. `std::pair`, `std::tuple`) are spread into the transformer unless it accepts the value
 * directly. Unlike `and_then`, the transformer's result is not run as an IO.
 *
 * The transformer must be a complete, non-reference, move constructible class or function pointer
 * type, callable as a const lvalue with the source's value (directly or spread), and must return a
 * valid plain value (not void, not a reference, not an IO, move constructible). An invalid pair
 * (including a non-IO source) is rejected by a friendly static_assert and yields
 * `detail::TransformError`.
 *
 * The source IO and transformer are taken by forwarding reference and perfect-forwarded into the
 * result IO's action, so an rvalue source moves (rvalue chains move with no extra copies) and an
 * lvalue source is copied (leaving the caller's IO untouched). The transformer is decayed, so an
 * lvalue transformer is copied and an rvalue moved into the result.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to IOTraits.
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param transformer Transformer applied to the source's value.
 * @return IO running the source IO and producing the transformer's plain value, or
 * detail::TransformError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <class TTraits, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    using source_io_type = std::remove_cvref_t<TSource>;
    // Decay the transformer for by-value validation and storage: lvalues are copied, rvalues are
    // moved, and function lvalues become function pointers.
    using transformer_type = std::decay_t<TTransformer>;
    static constexpr auto reason =
        TTraits::validate_transform(^^source_io_type, ^^transformer_type);
    static_assert(
        reason.empty(),
        detail::transform_error_msg<^^source_io_type, ^^transformer_type>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::TransformAction<source_io_type, transformer_type>;
        return IO<action_type, TTraits>{action_type{FW(source), FW(transformer)}};
    }
    if constexpr (!reason.empty())
    {
        return detail::TransformError{};
    }
}

/**
 * Transforms a source IO's value, naming the result.
 *
 * This overload allows the returned IO type to be named, for use in diagnostics.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My transform"_ss`).
 * @tparam TSource Source IO type, deduced (any value category; a non-IO source is rejected by
 * validation).
 * @tparam TTransformer Transformer taking the source's value and returning a plain value.
 * @param source Source IO to run (a non-IO source is rejected by validation).
 * @param transformer Transformer applied to the source's value.
 * @return IO running the source IO and producing the transformer's plain value with
 * IOTraits<name_cstr>, or detail::TransformError if the pair is invalid (which is also rejected by
 * a friendly static_assert).
 */
template <char const * name_cstr, class TSource, class TTransformer>
[[nodiscard]] constexpr auto transform(TSource && source, TTransformer && transformer)
{
    // cpplint mistakes this for std::transform; no <algorithm> is used here.
    // NOLINTNEXTLINE(build/include_what_you_use)
    return transform<IOTraits<name_cstr>>(FW(source), FW(transformer));
}
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
