// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file result.hpp
 *
 * Owned success, error and stopped results for effectful computations.
 */
#pragma once

#include <meta>

#include <expected>
#include <optional>
#include <type_traits>
#include <utility>

namespace fnfelt
{
/// Uninhabited error domain for computations that cannot report an error.
struct Infallible
{
    Infallible() = delete;
};

/**
 * Owned error payload identifying a Result's error channel.
 *
 * @tparam TErrorPayload Opaque error type.
 */
template <class TErrorPayload>
struct Error;
}  // namespace fnfelt

#include "detail/result.hpp"

namespace fnfelt
{
/**
 * Owned error payload identifying a Result's error channel.
 *
 * @tparam TErrorPayload Opaque error type.
 */
template <class TErrorPayload>
struct Error
{
    /// Reported error data.
    typename detail::ErrorType<TErrorPayload>::type value;
};

// The checked member's dependent type prevents implicit aggregate deduction.
/**
 * Deduce the owned error type from its value.
 *
 * The argument supplies the error payload to be owned.
 *
 * @tparam TErrorPayload Owned error type.
 */
template <class TErrorPayload>
Error(TErrorPayload) -> Error<TErrorPayload>;

/**
 * A successful value, an owned error, or a stopped computation.
 *
 * Disengagement means stopped. An engaged expected contains either the successful payload or an
 * Error whose value holds opaque error data. Nested optional and expected payloads remain data.
 * The result::success, result::error and result::stopped factories construct each state.
 *
 * @tparam TSuccessPayload Successful payload type or void.
 * @tparam TErrorPayload Error type, defaulting to Infallible.
 */
template <class TSuccessPayload, class TErrorPayload = Infallible>
using Result = typename detail::ResultType<TSuccessPayload, TErrorPayload>::type;

namespace result
{
/**
 * Construct a successful Result in place.
 *
 * With a void successful payload, supply no constructor arguments.
 *
 * @tparam TResult Destination Result type.
 * @tparam TArgs Successful payload constructor argument types.
 * @param args Successful payload constructor arguments.
 * @return Successful Result.
 */
template <class TResult, class... TArgs>
constexpr auto success(TArgs &&... args)
{
    return detail::ResultFactory<TResult>::success(std::forward<TArgs>(args)...);
}

/**
 * Construct an error Result, or an implicitly convertible error token.
 *
 * Omitting TResult returns an unexpected owning Error. Its deduced payload type must match the
 * destination Result's error type; specify TResult to construct or convert that payload explicitly.
 *
 * @tparam TResult Destination Result type, or void to deduce an unexpected error token.
 * @tparam TErrorPayload Error payload argument type.
 * @param error_payload Error payload to own.
 * @return Failed Result, or unexpected Error when TResult is omitted.
 */
template <class TResult = void, class TErrorPayload>
constexpr auto error(TErrorPayload && error_payload)
{
    if constexpr (std::is_void_v<TResult>)
    {
        return std::unexpected<Error<std::decay_t<TErrorPayload>>>{
            std::in_place, std::forward<TErrorPayload>(error_payload)};
    }
    else
    {
        return detail::ResultFactory<TResult>::error(std::forward<TErrorPayload>(error_payload));
    }
}

/**
 * Construct a stopped Result, or its implicitly convertible stop token.
 *
 * @tparam TResult Destination Result type, or void to return std::nullopt_t.
 * @return Stopped Result, or std::nullopt when TResult is omitted.
 */
template <class TResult = void>
constexpr auto stopped()
{
    if constexpr (std::is_void_v<TResult>)
    {
        return std::nullopt;
    }
    else
    {
        return detail::ResultFactory<TResult>::stopped();
    }
}

/**
 * Whether a type has the marked Result representation.
 *
 * Top-level cv/ref qualification does not affect recognition.
 *
 * @tparam T Type to inspect.
 * @return Whether T is a Result type.
 */
template <class T>
consteval bool is_result() noexcept
{
    return detail::ResultTraits::is_result_type(^^T);
}

/**
 * Whether a value's type has the marked Result representation.
 *
 * The value is not read or copied, so runtime objects can be queried in constant expressions.
 *
 * @tparam T Deduced value type.
 * @param source Value whose type is inspected.
 * @return Whether T is a Result type.
 */
template <class T>
constexpr bool is_result([[maybe_unused]] T && source) noexcept
{
    return is_result<T>();
}

/**
 * Whether the computation stopped without a successful payload or an error.
 *
 * @tparam TResult Source Result type.
 * @param source Result to inspect.
 * @return Whether the outer Result is disengaged.
 */
template <class TResult>
constexpr bool is_stopped(TResult const & source) noexcept
{
    auto const & checked_source = detail::ResultAccess<TResult const &>::forward(source);
    return !checked_source.has_value();
}

/**
 * Whether the computation reported an error.
 *
 * @tparam TResult Source Result type.
 * @param source Result to inspect.
 * @return Whether the Result contains an error.
 */
template <class TResult>
constexpr bool is_error(TResult const & source) noexcept
{
    auto const & checked_source = detail::ResultAccess<TResult const &>::forward(source);
    return checked_source && !checked_source->has_value();
}

/**
 * Whether the computation completed successfully.
 *
 * @tparam TResult Source Result type.
 * @param source Result to inspect.
 * @return Whether the Result contains a successful payload or successful void.
 */
template <class TResult>
constexpr bool is_success(TResult const & source) noexcept
{
    auto const & checked_source = detail::ResultAccess<TResult const &>::forward(source);
    return checked_source && checked_source->has_value();
}

/**
 * Access the successful payload without checking the Result state.
 *
 * The source must be successful. References preserve source qualification; a reference returned
 * from an rvalue must not outlive that source. Successful void returns void.
 *
 * @tparam TResult Source Result expression type.
 * @param source Successful Result to access.
 * @return Successful payload with matching cv/ref qualification, or void.
 */
template <class TResult>
constexpr decltype(auto) get_success(TResult && source) noexcept
{
    return **detail::ResultAccess<TResult>::forward(std::forward<TResult>(source));
}

/**
 * Access the error payload without checking the Result state.
 *
 * The source must contain an error. References preserve source qualification; a reference returned
 * from an rvalue must not outlive that source. Only the Result's own Error wrapper is removed.
 *
 * @tparam TResult Source Result expression type.
 * @param source Failed Result to access.
 * @return Opaque error payload with matching cv/ref qualification.
 */
template <class TResult>
constexpr decltype(auto) get_error(TResult && source) noexcept
{
    return ((*detail::ResultAccess<TResult>::forward(std::forward<TResult>(source))).error().value);
}

/**
 * Move a successful payload into an owned optional, or return an empty optional.
 *
 * Requires an rvalue whose payload can be constructed in std::optional; const rvalues may copy.
 * The source keeps its state with a possibly moved-from payload. Successful void uses
 * std::monostate.
 *
 * @tparam TResult Source Result expression type.
 * @param source Result rvalue to inspect and consume when successful.
 * @return Optional of the declared successful payload, or std::optional<std::monostate> for void.
 */
template <class TResult>
constexpr auto maybe_get_success(TResult && source)
{
    return detail::ResultAccess<TResult>::template maybe_get<false>(std::forward<TResult>(source));
}

/**
 * Move an error payload into an owned optional, or return an empty optional.
 *
 * Requires an rvalue whose payload can be constructed in std::optional; const rvalues may copy.
 * The source keeps its state with a possibly moved-from payload. Only its own Error wrapper is
 * removed.
 *
 * @tparam TResult Source Result expression type.
 * @param source Result rvalue to inspect and consume when it contains an error.
 * @return Optional of the declared opaque error payload.
 */
template <class TResult>
constexpr auto maybe_get_error(TResult && source)
{
    return detail::ResultAccess<TResult>::template maybe_get<true>(std::forward<TResult>(source));
}
}  // namespace result
}  // namespace fnfelt
