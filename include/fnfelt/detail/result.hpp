// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include <fnfelt/result.hpp>
/**
 * @file detail/result.hpp
 *
 * Validation, reflection and pure operations for the fixed Result representation.
 */
#pragma once

#include <meta>

#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <fnfelt/detail/invocability.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt
{
struct Infallible;
template <class TErrorPayload>
struct Error;

namespace detail
{
/// Injectable owned-error validation.
struct ErrorTraits
{
    /**
     * Validate an owned error payload type.
     *
     * @param error_payload_meta Error type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate(std::meta::info error_payload_meta)
    {
        if (!is_type(error_payload_meta) || !is_complete_type(error_payload_meta) ||
            !is_object_type(error_payload_meta) || is_array_type(error_payload_meta))
        {
            return "Error payload must be a complete non-array object type";
        }
        if (is_abstract_type(error_payload_meta) || !is_destructible_type(error_payload_meta))
        {
            return "Error payload must be a concrete destructible type";
        }
        return {};
    }
};

/**
 * Checked owned-error field type with injectable validation.
 *
 * @tparam TErrorPayload Owned error type.
 * @tparam TTraits Owned-error validation policy.
 */
template <class TErrorPayload, class TTraits = ErrorTraits>
struct ErrorType
{
    /// Validation diagnostic.
    static constexpr auto reason = TTraits::validate(^^TErrorPayload);
    static_assert(reason.empty(), reason);
    /// Validated owned type.
    using type = std::conditional_t<reason.empty(), TErrorPayload, Infallible>;
};

/// Injectable result validation.
struct ResultTraits
{
    /**
     * Validate successful and error payload types.
     *
     * @param success_payload_meta Successful payload type or void.
     * @param error_payload_meta Owned error type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate(
        std::meta::info success_payload_meta, std::meta::info error_payload_meta)
    {
        if (!is_type(success_payload_meta) || !is_type(error_payload_meta))
        {
            return "Result success payload and error arguments must be types";
        }
        if (!is_void_type(success_payload_meta))
        {
            if (!is_complete_type(success_payload_meta) || !is_object_type(success_payload_meta) ||
                is_array_type(success_payload_meta))
            {
                return "Result success payload must be void or a complete non-array object type";
            }
            if (is_abstract_type(success_payload_meta) ||
                !is_destructible_type(success_payload_meta))
            {
                return "Result success payload must be a concrete destructible type";
            }
            auto const bare_success_payload = dealias(remove_cvref(success_payload_meta));
            if (is_same_type(bare_success_payload, ^^std::in_place_t) ||
                is_same_type(bare_success_payload, ^^std::unexpect_t) ||
                (has_template_arguments(bare_success_payload) &&
                 template_of(bare_success_payload) == ^^std::unexpected))
            {
                return "Result success payload cannot be an expected construction tag or "
                       "std::unexpected";
            }
        }
        return ErrorTraits::validate(error_payload_meta);
    }

    /**
     * Whether a type has the fixed Result representation.
     *
     * @param source_meta Source type.
     * @return Whether its outer wrappers represent a Result.
     */
    static consteval bool is_result_type(std::meta::info source_meta)
    {
        source_meta = dealias(remove_cvref(source_meta));
        if (!has_template_arguments(source_meta) || template_of(source_meta) != ^^std::optional)
        {
            return false;
        }
        auto const expected_meta = dealias(template_arguments_of(source_meta)[0]);
        if (is_const_type(expected_meta) || is_volatile_type(expected_meta) ||
            !has_template_arguments(expected_meta) || template_of(expected_meta) != ^^std::expected)
        {
            return false;
        }
        auto const error_meta = dealias(template_arguments_of(expected_meta)[1]);
        if (is_const_type(error_meta) || is_volatile_type(error_meta) ||
            !has_template_arguments(error_meta) || template_of(error_meta) != ^^fnfelt::Error)
        {
            return false;
        }
        return true;
    }

    /**
     * Reflect a Result's successful payload without interpreting nested data.
     *
     * @param result_meta Result type.
     * @return Successful payload type.
     */
    static consteval std::meta::info success_payload_meta(std::meta::info result_meta)
    {
        auto const expected_meta = template_arguments_of(dealias(remove_cvref(result_meta)))[0];
        return template_arguments_of(dealias(expected_meta))[0];
    }

    /**
     * Reflect a Result's opaque error domain.
     *
     * @param result_meta Result type.
     * @return Owned error type.
     */
    static consteval std::meta::info error_payload_meta(std::meta::info result_meta)
    {
        auto const expected_meta = template_arguments_of(dealias(remove_cvref(result_meta)))[0];
        auto const error_meta = template_arguments_of(dealias(expected_meta))[1];
        return template_arguments_of(dealias(error_meta))[0];
    }

    /**
     * Reflect the fixed Result representation.
     *
     * @param success_payload_meta Successful payload type.
     * @param error_payload_meta Owned error type.
     * @return Result type.
     */
    static consteval std::meta::info type_meta(
        std::meta::info success_payload_meta, std::meta::info error_payload_meta)
    {
        auto const error_meta = substitute(^^Error, {error_payload_meta});
        auto const expected_meta = substitute(^^std::expected, {success_payload_meta, error_meta});
        return substitute(^^std::optional, {expected_meta});
    }

    /**
     * Validate a destination for owned Result construction.
     *
     * @param result_meta Destination type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate_construction(std::meta::info result_meta)
    {
        if (!is_type(result_meta) || is_reference_type(result_meta) || is_const_type(result_meta) ||
            is_volatile_type(result_meta) || !is_result_type(result_meta))
        {
            return "Result construction requires an unqualified Result type";
        }
        return validate(success_payload_meta(result_meta), error_payload_meta(result_meta));
    }

    /**
     * Validate a Result expression for state inspection and payload access.
     *
     * @param result_meta Source expression type.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate_access(std::meta::info result_meta)
    {
        if (!is_type(result_meta) || is_volatile_type(remove_reference(result_meta)) ||
            !is_result_type(result_meta))
        {
            return "Result access requires a non-volatile Result type";
        }
        return validate(success_payload_meta(result_meta), error_payload_meta(result_meta));
    }

    /**
     * Validate consuming optional access to one Result payload.
     *
     * @param result_meta Source expression type, including its value category.
     * @param is_error_branch Whether to inspect the error rather than successful payload.
     * @return Empty on success, otherwise a diagnostic.
     */
    static consteval std::string_view validate_maybe_access(
        std::meta::info result_meta, bool is_error_branch)
    {
        if (auto const reason = validate_access(result_meta); !reason.empty())
        {
            return reason;
        }
        if (!is_rvalue_reference_type(result_meta))
        {
            return "Result optional access requires an rvalue; use std::move(source)";
        }

        auto const payload_meta =
            is_error_branch ? error_payload_meta(result_meta) : success_payload_meta(result_meta);
        if (is_void_type(payload_meta))
        {
            // Successful void is represented by an engaged optional unit value.
            return {};
        }
        auto const bare_payload = remove_cvref(payload_meta);
        if (is_same_type(bare_payload, ^^std::nullopt_t) ||
            is_same_type(bare_payload, ^^std::in_place_t))
        {
            return "Result optional access cannot contain std::nullopt_t or std::in_place_t";
        }

        auto argument_meta = payload_meta;
        if (is_const_type(remove_reference(result_meta)))
        {
            argument_meta = add_const(argument_meta);
        }
        argument_meta = add_rvalue_reference(argument_meta);
        if (!is_constructible_type(payload_meta, {argument_meta}))
        {
            return "Result optional access cannot construct its payload from the source rvalue";
        }
        return {};
    }
};

/**
 * Checked construction of the fixed standard-library representation.
 *
 * @tparam TSuccessPayload Successful payload type or void.
 * @tparam TErrorPayload Owned error type.
 * @tparam TTraits Result validation policy.
 */
template <class TSuccessPayload, class TErrorPayload, class TTraits = ResultTraits>
struct ResultType
{
    /// Validation diagnostic.
    static constexpr auto reason = TTraits::validate(^^TSuccessPayload, ^^TErrorPayload);
    static_assert(reason.empty(), reason);

    // Safe fallbacks prevent cascading standard-library errors after the diagnostic above.
    /// Validated successful payload.
    using checked_success_payload =
        std::conditional_t<reason.empty(), TSuccessPayload, std::monostate>;
    /// Validated error payload.
    using checked_error_payload = std::conditional_t<reason.empty(), TErrorPayload, Infallible>;
    /// Fixed standard-library representation.
    using type =
        std::optional<std::expected<checked_success_payload, Error<checked_error_payload>>>;
};

/**
 * Owned Result construction with injectable destination validation.
 *
 * @tparam TResult Destination Result type.
 * @tparam TTraits Destination validation policy.
 */
template <class TResult, class TTraits = ResultTraits>
struct ResultFactory
{
    /// Destination validation diagnostic.
    static constexpr auto reason = TTraits::validate_construction(^^TResult);
    static_assert(reason.empty(), reason);
    /// Validated destination, with a fallback preventing cascading construction errors.
    using type = std::conditional_t<
        reason.empty(),
        TResult,
        typename ResultType<std::monostate, Infallible>::type>;

    /**
     * Construct a successful payload in place, or successful void without arguments.
     *
     * @tparam TArgs Successful payload constructor argument types.
     * @param args Successful payload constructor arguments.
     * @return Successful Result.
     */
    template <class... TArgs>
    static constexpr type success(TArgs &&... args)
    {
        if constexpr (reason.empty())
        {
            return type{std::in_place, std::in_place, FW(args)...};
        }
        else
        {
            return {};
        }
    }

    /**
     * Construct an error in the destination's error domain.
     *
     * @tparam TErrorPayload Error payload argument type.
     * @param error_payload Error payload to construct or convert.
     * @return Failed Result.
     */
    template <class TErrorPayload>
    static constexpr type error(TErrorPayload && error_payload)
    {
        if constexpr (reason.empty())
        {
            using error_payload_type = [:ResultTraits::error_payload_meta(^^type):];
            using error_type = Error<error_payload_type>;
            if constexpr (
                is_same_type(remove_cvref(^^TErrorPayload), remove_cvref(^^error_payload_type)) &&
                is_constructible_type(^^error_type, {^^TErrorPayload && }))
            {
                // Exact payloads can enter the owned error directly without an extra move.
                return type{std::in_place, std::unexpect, FW(error_payload)};
            }
            else
            {
                // Convert opaque payload data before wrapping; never interpret an argument's Error.
                return type{
                    std::in_place,
                    std::unexpect,
                    error_type{error_payload_type(FW(error_payload))}};
            }
        }
        else
        {
            return {};
        }
    }

    /**
     * Construct a stopped Result without constructing either payload.
     *
     * @return Stopped Result.
     */
    static constexpr type stopped()
    {
        return std::nullopt;
    }
};

/**
 * Result expression validation with an injectable access policy.
 *
 * @tparam TResult Source expression type.
 * @tparam TTraits Access validation policy.
 */
template <class TResult, class TTraits = ResultTraits>
struct ResultAccess
{
    /// Source validation diagnostic.
    static constexpr auto reason = TTraits::validate_access(^^TResult);
    static_assert(reason.empty(), reason);
    /// Validated source, with a fallback preventing cascading access errors.
    using type = std::conditional_t<
        reason.empty(),
        TResult,
        typename ResultType<std::monostate, Infallible>::type>;

    /**
     * Forward the validated source without copying its payloads.
     *
     * @param source Source Result expression.
     * @return Source with its original cv/ref qualification.
     */
    static constexpr decltype(auto) forward(TResult && source) noexcept
    {
        if constexpr (reason.empty())
        {
            return FW(source);
        }
        else
        {
            return type{};
        }
    }

    /**
     * Move the matching payload into an optional without moving the other branch.
     *
     * @tparam is_error_branch Whether to extract the error rather than successful payload.
     * @param source Source Result rvalue.
     * @return Owned optional payload, or std::optional<std::monostate> for successful void.
     */
    template <bool is_error_branch>
    static constexpr auto maybe_get(TResult && source)
    {
        if constexpr (reason.empty())
        {
            static constexpr auto maybe_reason =
                TTraits::validate_maybe_access(^^TResult &&, is_error_branch);
            static_assert(maybe_reason.empty(), maybe_reason);
            if constexpr (maybe_reason.empty())
            {
                using payload_type = [:is_error_branch
                                          ? ResultTraits::error_payload_meta(^^TResult)
                                          : ResultTraits::success_payload_meta(^^TResult):];
                using optional_type = std::optional<
                    std::conditional_t<std::is_void_v<payload_type>, std::monostate, payload_type>>;
                if (!source)
                {
                    return optional_type{};
                }
                if constexpr (is_error_branch)
                {
                    if (source->has_value())
                    {
                        return optional_type{};
                    }
                    return optional_type{std::in_place, ((*FW(source)).error().value)};
                }
                else
                {
                    if (!source->has_value())
                    {
                        return optional_type{};
                    }
                    if constexpr (std::is_void_v<payload_type>)
                    {
                        return optional_type{std::in_place};
                    }
                    else
                    {
                        return optional_type{std::in_place, **FW(source)};
                    }
                }
            }
            else
            {
                // Do not instantiate optional for a rejected tag or unconstructible payload.
                return std::optional<std::monostate>{};
            }
        }
        else
        {
            return std::optional<std::monostate>{};
        }
    }
};

/// Successful payload and opaque error domain for Result composition.
struct ResultTypeInfo
{
    /// Whether the chain has entered Result mode.
    bool is_result = false;
    /// Whether the error domain is inhabited.
    bool has_error = false;
    /// Successful payload type.
    std::meta::info success_payload_meta = ^^void;
    /// Error domain, with Infallible denoting no reported errors.
    std::meta::info error_payload_meta = ^^Infallible;
    /// Diagnostic when error domains cannot be joined.
    std::string_view reason;

    /**
     * Describe only the outer Result, leaving nested payloads opaque.
     *
     * @param source_meta Source value type.
     * @param should_unwrap Whether to interpret the outer Result.
     * @return Successful payload and error domain.
     */
    static consteval ResultTypeInfo from(std::meta::info source_meta, bool should_unwrap = true)
    {
        ResultTypeInfo result;
        result.success_payload_meta = source_meta;
        if (should_unwrap && ResultTraits::is_result_type(source_meta))
        {
            result.is_result = true;
            result.success_payload_meta = ResultTraits::success_payload_meta(source_meta);
            result.error_payload_meta = ResultTraits::error_payload_meta(source_meta);
            result.has_error = !is_same_type(result.error_payload_meta, ^^Infallible);
        }
        return result;
    }

    /**
     * Join error domains while retaining the subsequent successful payload.
     *
     * @param next Subsequent value's channels.
     * @return Joined channels or an incompatible-error diagnostic.
     */
    consteval ResultTypeInfo merge(ResultTypeInfo next) const
    {
        if (!reason.empty())
        {
            next.reason = reason;
        }
        if (has_error && next.has_error &&
            !is_same_type(error_payload_meta, next.error_payload_meta))
        {
            next.reason =
                "composed error channels must have the same error type; use transform_error "
                "on conflicting computations before composition";
        }
        if (has_error && !next.has_error)
        {
            next.has_error = true;
            next.error_payload_meta = error_payload_meta;
        }
        next.is_result = is_result || next.is_result;
        return next;
    }

    /**
     * Validate the composed payload and error domain.
     *
     * @return Empty on success, otherwise a diagnostic.
     */
    consteval std::string_view validate() const
    {
        if (!reason.empty())
        {
            return reason;
        }
        if (is_result)
        {
            return ResultTraits::validate(success_payload_meta, error_payload_meta);
        }
        return {};
    }

    /**
     * Reflect the plain value or fixed Result representation.
     *
     * @return Composed value type.
     */
    consteval std::meta::info type_meta() const
    {
        return is_result ? ResultTraits::type_meta(success_payload_meta, error_payload_meta)
                         : success_payload_meta;
    }
};

/// Selected successful-payload invocation and its declared result.
struct SuccessContinuationInfo
{
    /// Whether the source is a Result.
    bool is_result = false;
    /// Whether the continuation accepts the selected payload.
    bool is_valid = false;
    /// Argument type, with void denoting nullary invocation.
    std::meta::info argument_meta = ^^void;
    /// Continuation's declared result.
    std::meta::info result_meta = ^^void;

    /**
     * Select ordinary data or a Result's successful payload for invocation.
     *
     * @param continuation_meta Continuation type.
     * @param source_meta Source type.
     * @return Selected invocation or an invalid binding.
     */
    static consteval SuccessContinuationInfo
    from(std::meta::info continuation_meta, std::meta::info source_meta)
    {
        auto const is_result = ResultTraits::is_result_type(source_meta);
        // Unwrap one Result layer; plain sources remain the argument type.
        auto const success_payload_meta =
            is_result ? ResultTraits::success_payload_meta(source_meta) : source_meta;
        if (is_void_type(success_payload_meta))
        {
            // Void success supplies no argument, so only nullary invocation applies.
            if (is_invocable_type(continuation_meta, {}))
            {
                // Reflect the declared result only after confirming the invocation is valid.
                return {
                    is_result, true, success_payload_meta, invoke_result(continuation_meta, {})};
            }
        }
        else if (is_dispatch_invocable(continuation_meta, success_payload_meta))
        {
            // Record the accepted payload and declared result; direct invocation wins over spread.
            return {
                is_result,
                true,
                success_payload_meta,
                dispatch_result_meta(continuation_meta, success_payload_meta)};
        }
        // Neither nullary nor direct/spread invocation is valid; leave the binding invalid.
        return {};
    }
};

/**
 * Construct success without interpreting the successful payload's internals.
 *
 * @tparam TResult Destination plain value or Result type.
 * @tparam TSuccessPayload Successful payload argument type.
 * @param success_payload Successful payload.
 * @return Plain value or successful Result.
 */
template <class TResult, class TSuccessPayload>
constexpr TResult make_success_result(TSuccessPayload && success_payload)
{
    if constexpr (ResultTraits::is_result_type(^^TResult))
    {
        return TResult{std::in_place, std::in_place, FW(success_payload)};
    }
    else
    {
        return FW(success_payload);
    }
}

/**
 * Construct a successful void Result.
 *
 * @tparam TResult Destination Result type with a void payload.
 * @return Successful void Result.
 */
template <class TResult>
constexpr TResult make_void_success()
{
    return TResult{std::in_place, std::in_place};
}

/**
 * Construct a reported error without inspecting its opaque domain.
 *
 * @tparam TResult Destination Result type.
 * @tparam TErrorPayload Error payload argument type.
 * @param error_payload Error payload.
 * @return Failed Result.
 */
template <class TResult, class TErrorPayload>
constexpr TResult make_error_result(TErrorPayload && error_payload)
{
    return TResult{std::in_place, std::unexpect, FW(error_payload)};
}

/**
 * Promote ordinary success or widen an Infallible Result to the joined error domain.
 *
 * @tparam TResult Destination plain value or Result type.
 * @tparam TSourceResult Source logical result argument type.
 * @param source_result Source logical result.
 * @return Composed result.
 */
template <class TResult, class TSourceResult>
constexpr TResult convert_result(TSourceResult && source_result)
{
    if constexpr (is_same_type(^^TResult, remove_cvref(^^TSourceResult)))
    {
        // Preserve the complete value without unpacking and rebuilding its payload.
        return FW(source_result);
    }
    else if constexpr (ResultTraits::is_result_type(^^TSourceResult))
    {
        // Widen only the outer Result; nested wrappers remain ordinary payload data.
        if (!source_result)
        {
            // A stopped computation stays stopped in the joined error domain.
            return std::nullopt;
        }
        if constexpr (!is_same_type(
                          ResultTraits::error_payload_meta(^^TSourceResult), ^^Infallible))
        {
            // Infallible has no error to extract, so instantiate this path only for owned errors.
            if (!source_result->has_value())
            {
                // Preserve the reported error instead of constructing a successful payload.
                return make_error_result<TResult>((*FW(source_result)).error().value);
            }
        }
        if constexpr (is_void_type(ResultTraits::success_payload_meta(^^TSourceResult)))
        {
            // Void success carries only its state, with no payload to forward.
            return make_void_success<TResult>();
        }
        else
        {
            // Forward the successful payload into the joined Result without interpreting it.
            return make_success_result<TResult>(**FW(source_result));
        }
    }
    else
    {
        // Plain values represent success: wrap them only when the destination is a Result.
        return make_success_result<TResult>(FW(source_result));
    }
}

/**
 * Inspect terminal states without invoking the success continuation.
 *
 * @tparam is_result Whether the source has a Result layer to inspect.
 * @tparam TResult Destination Result type.
 * @tparam TSourceResult Source logical result argument type.
 * @param source_result Source logical result.
 * @return A propagated stop or error, or no value when the success path should run.
 */
template <bool is_result, class TResult, class TSourceResult>
constexpr std::optional<TResult> maybe_stop_or_error(TSourceResult && source_result)
{
    if constexpr (is_result)
    {
        if (!source_result)
        {
            return std::optional<TResult>{std::in_place, std::nullopt};
        }
        if constexpr (!is_same_type(
                          ResultTraits::error_payload_meta(^^TSourceResult), ^^Infallible))
        {
            if (!source_result->has_value())
            {
                return std::optional<TResult>{
                    std::in_place, make_error_result<TResult>((*FW(source_result)).error().value)};
            }
        }
    }
    return std::nullopt;
}

/**
 * Invoke a continuation on ordinary data or a successful Result payload.
 *
 * @tparam is_result Whether to unwrap the source's successful Result payload.
 * @tparam TContinuation Continuation argument type.
 * @tparam TSourceResult Source result argument type.
 * @param continuation Continuation to invoke.
 * @param source_result Successful source result.
 * @return Continuation result.
 */
template <bool is_result, class TContinuation, class TSourceResult>
constexpr decltype(auto) invoke_success_continuation(
    TContinuation && continuation, TSourceResult && source_result)
{
    if constexpr (is_result)
    {
        if constexpr (is_void_type(ResultTraits::success_payload_meta(^^TSourceResult)))
        {
            return std::invoke(FW(continuation));
        }
        else
        {
            return fnfelt::detail::invoke_dispatch(FW(continuation), **FW(source_result));
        }
    }
    else
    {
        return fnfelt::detail::invoke_dispatch(FW(continuation), FW(source_result));
    }
}

/**
 * Map successful values while preserving the source's Result layer.
 *
 * @tparam TResult Destination plain value or Result type.
 * @tparam TTransformer Successful-payload mapper argument type.
 * @tparam TSourceResult Source logical result argument type.
 * @param transformer Successful-payload mapper.
 * @param source_result Source logical result.
 * @return Mapped value, or the source's stop or error.
 */
template <class TResult, class TTransformer, class TSourceResult>
constexpr TResult map_success_result(TTransformer && transformer, TSourceResult && source_result)
{
    if constexpr (!ResultTraits::is_result_type(^^TSourceResult))
    {
        // Plain sources adopt the mapper's result directly, including its first Result layer.
        return invoke_success_continuation<false>(FW(transformer), FW(source_result));
    }
    else
    {
        if (!source_result)
        {
            // A stopped source must not invoke the mapper.
            return std::nullopt;
        }
        if constexpr (!is_same_type(
                          ResultTraits::error_payload_meta(^^TSourceResult), ^^Infallible))
        {
            if (!source_result->has_value())
            {
                // Preserve the error without forming an impossible Infallible construction.
                return make_error_result<TResult>((*FW(source_result)).error().value);
            }
        }
        if constexpr (is_void_type(^^decltype(invoke_success_continuation<true>(
                          FW(transformer), FW(source_result)))))
        {
            // A void mapper still leaves the source Result layer successfully engaged.
            invoke_success_continuation<true>(FW(transformer), FW(source_result));
            return make_void_success<TResult>();
        }
        else
        {
            // Mapper-returned Results are payload data inside the existing source layer.
            return make_success_result<TResult>(
                invoke_success_continuation<true>(FW(transformer), FW(source_result)));
        }
    }
}

/// Reachable error-payload invocation and its uninterpreted declared result.
struct ErrorContinuationInfo
{
    /// Whether the source can report an error.
    bool is_reachable = false;
    /// Whether the handler accepts the source's error payload.
    bool is_valid = false;
    /// Opaque error argument type.
    std::meta::info argument_meta = ^^Infallible;
    /// Handler's declared result, without interpreting IO or Result wrappers.
    std::meta::info result_meta = ^^void;

    /**
     * Select only a reachable error payload for direct or spread invocation.
     *
     * @param continuation_meta Error handler type.
     * @param source_meta Source logical value type.
     * @return Reachability, accepted argument and raw declared result.
     */
    static consteval ErrorContinuationInfo
    from(std::meta::info continuation_meta, std::meta::info source_meta)
    {
        ErrorContinuationInfo result;
        if (!ResultTraits::is_result_type(source_meta))
        {
            return result;
        }
        result.argument_meta = ResultTraits::error_payload_meta(source_meta);
        result.is_reachable = !is_same_type(result.argument_meta, ^^Infallible);
        // An impossible channel must not instantiate a generic handler's invalid body.
        if (result.is_reachable && is_dispatch_invocable(continuation_meta, result.argument_meta))
        {
            result.is_valid = true;
            result.result_meta = dispatch_result_meta(continuation_meta, result.argument_meta);
        }
        return result;
    }
};

/**
 * Preserve successful values and any outcomes without a compatible handler.
 *
 * @tparam handles_error Whether the reachable error has a compatible handler.
 * @tparam TResult Destination Result type.
 * @tparam TSourceResult Canonical source result argument type.
 * @param source_result Canonical source result.
 * @return Engaged passthrough value, or empty when recovery should run.
 */
template <bool handles_error, class TResult, class TSourceResult>
constexpr std::optional<TResult> maybe_stop_or_success(TSourceResult && source_result)
{
    using result_type = TResult;
    if (!source_result)
    {
        return std::optional<result_type>{std::in_place, std::nullopt};
    }
    if constexpr (!is_same_type(ResultTraits::error_payload_meta(^^TSourceResult), ^^Infallible))
    {
        if (!source_result->has_value())
        {
            if constexpr (handles_error)
            {
                return std::nullopt;
            }
            else
            {
                return std::optional<result_type>{
                    std::in_place,
                    make_error_result<result_type>((*FW(source_result)).error().value)};
            }
        }
    }
    if constexpr (is_void_type(ResultTraits::success_payload_meta(^^TSourceResult)))
    {
        return std::optional<result_type>{std::in_place, make_void_success<result_type>()};
    }
    else
    {
        return std::optional<result_type>{
            std::in_place, make_success_result<result_type>(**FW(source_result))};
    }
}

/**
 * Invoke an error handler with the common error payload, directly or spread.
 *
 * @tparam TContinuation Error recovery continuation argument type.
 * @tparam TSourceResult Failed source result argument type.
 * @param continuation Error recovery continuation.
 * @param source_result Failed canonical source result.
 * @return Handler's declared result.
 */
template <class TContinuation, class TSourceResult>
constexpr decltype(auto) invoke_error_continuation(
    TContinuation && continuation, TSourceResult && source_result)
{
    return fnfelt::detail::invoke_dispatch(FW(continuation), (*FW(source_result)).error().value);
}

/**
 * Map errors and pass through successful or skipped outcomes.
 *
 * @tparam TResult Destination Result type.
 * @tparam TTransformer Error mapper argument type.
 * @tparam TSourceResult Source Result argument type.
 * @param transformer Error mapper.
 * @param source_result Source Result.
 * @return Result with its mapped error domain.
 */
template <class TResult, class TTransformer, class TSourceResult>
constexpr TResult map_error_result(TTransformer && transformer, TSourceResult && source_result)
{
    if constexpr (is_same_type(ResultTraits::error_payload_meta(^^TSourceResult), ^^Infallible))
    {
        // Do not form an invocation for an impossible channel, even in constant evaluation.
        return FW(source_result);
    }
    else
    {
        if (!source_result)
        {
            return std::nullopt;
        }
        if (!source_result->has_value())
        {
            return make_error_result<TResult>(
                invoke_error_continuation(FW(transformer), FW(source_result)));
        }
        if constexpr (is_void_type(ResultTraits::success_payload_meta(^^TSourceResult)))
        {
            return make_void_success<TResult>();
        }
        else
        {
            return make_success_result<TResult>(**FW(source_result));
        }
    }
}

}  // namespace detail
}  // namespace fnfelt

#include <fnfelt/macros_pop.hpp>
