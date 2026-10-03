// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../io.hpp"
/**
 * @file bind.hpp
 *
 * Free `bind` operation for the IO monad (and its implementation details).
 *
 * This header includes IO.hpp, so the IO class is complete, and defines the free `bind` functions.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <fnfelt/detail/errors.hpp>
#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/fwd.hpp>
#include <fnfelt/monad/io/traits.hpp>

namespace fnfelt::monad::io
{
namespace detail
{

/**
 * Tombstone returned by `io::bind` when the kleisli is invalid.
 *
 * The invalid bind is already rejected by a friendly static_assert; this type exists only to keep
 * the return type well-formed and must never be used.
 */
struct BindError
{
};

/**
 * Action that runs a source IO and feeds its value to a kleisli continuation.
 *
 * The continuation is invoked with the value directly when possible, otherwise the value is spread
 * into the continuation as if by `std::apply` (direct invocation wins over spreading). The
 * continuation is a const member, so it must be callable as a const lvalue.
 *
 * @tparam TSourceIO Source IO to run.
 * @tparam TKleisli Continuation taking the source's value and returning an IO.
 */
template <class TSourceIO, class TKleisli>
struct BindAction
{
    /// Source IO to run.
    TSourceIO source;
    /// Continuation applied to the source's value.
    TKleisli kleisli;

    /**
     * Run the source IO, apply the continuation to its value, then run the resulting IO.
     *
     * @return The final value.
     */
    constexpr auto operator()() const
    {
        using source_value = typename TSourceIO::value;
        auto input = source();
        if constexpr (is_directly_invocable(^^TKleisli, ^^source_value))
        {
            auto next = kleisli(std::move(input));
            return next();
        }
        if constexpr (!is_directly_invocable(^^TKleisli, ^^source_value))
        {
            auto next = std::apply(kleisli, std::move(input));
            return next();
        }
    }
};

/**
 * Name of IO.
 *
 * @tparam io_meta IO reflection to check.
 * @return "<unknown>" if the IO reflection is not an IO, otherwise the IO's name.
 */
template <std::meta::info io_meta>
constexpr std::string_view maybe_io_name()
{
    if constexpr (is_io(io_meta))
    {
        return [:io_meta:] ::name;
    }

    return "<unknown>";
}

/**
 * Name of IO returned from kleisli.
 *
 * @tparam source_io_meta Source IO reflection.
 * @tparam kleisli_meta Kleisli continuation reflection.
 * @return Name of IO returned from kleisli if possible, otherwise "<unknown>".
 */
template <std::meta::info source_io_meta, std::meta::info kleisli_meta>
constexpr std::string_view maybe_kleisli_result_io_name()
{
    if constexpr (is_io(source_io_meta))
    {
        constexpr std::meta::info kleisli_arg_meta = io_value_meta(source_io_meta);

        if constexpr (is_invocable_type(kleisli_meta, {kleisli_arg_meta}))
        {
            constexpr std::meta::info kleisli_result_io =
                invoke_result(kleisli_meta, {kleisli_arg_meta});
            return maybe_io_name<kleisli_result_io>();
        }
    }

    return "<unknown>";
}

/**
 * Constructs a detailed error message for an invalid `io::bind` operation.
 *
 * Uses reflection to extract type names and diagnostics about the source IO and kleisli
 * continuation when bind validation fails. The message includes the IO name, the source IO type,
 * the kleisli result type (if determinable), the specific validation failure reason, and the
 * kleisli's display string.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam source_io_meta Reflection info for the source IO type.
 * @tparam kleisli_meta Reflection info for the kleisli continuation type.
 * @param bound_io_name Name of the bound IO operation (from traits).
 * @param reason Validation failure reason from `TTraits::validate_bind`.
 * @return Formatted error message string.
 */
template <std::meta::info source_io_meta, std::meta::info kleisli_meta>
consteval std::string error_msg(std::string_view bound_io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    msg += "IO bind error: ";
    msg += bound_io_name;
    msg += "{(";
    msg += maybe_io_name<source_io_meta>();
    msg += "()) => ";
    msg += maybe_kleisli_result_io_name<source_io_meta, kleisli_meta>();
    msg += "}: ";
    msg += reason;
    msg += ": ";
    msg += display_string_of(kleisli_meta);

    return msg;
}
}  // namespace detail

/**
 * Binds a continuation (kleisli) to a source IO, producing an IO of the continuation's value.
 *
 * The returned IO runs the source IO, applies the continuation to its value, then runs the
 * continuation's IO. Values that are specialisations with all-type template arguments (e.g.
 * `std::pair`, `std::tuple`) are spread into the continuation unless it accepts the value
 * directly.
 *
 * The continuation must be a complete, non-reference, move constructible class or function pointer
 * type, callable as a const lvalue with the source's value (directly or spread), and must return an
 * IO. An invalid pair is rejected by a friendly static_assert and yields `detail::BindError`.
 *
 * @tparam TTraits Traits used to validate the pair and for the result IO, defaulting to IOTraits.
 * @tparam TSourceAction Source IO's action.
 * @tparam TSourceTraits Source IO's traits, used only for the source's own construction.
 * @tparam TKleisli Continuation taking the source's value and returning an IO.
 * @param source Source IO to run.
 * @param kleisli Continuation applied to the source's value.
 * @return IO running the source IO followed by the continuation's IO, or detail::BindError if
 * the pair is invalid (which is also rejected by a friendly static_assert).
 */
template <class TTraits, class TSourceAction, class TSourceTraits, class TKleisli>
[[nodiscard]] constexpr auto bind(IO<TSourceAction, TSourceTraits> source, TKleisli kleisli)
{
    using source_io_type = IO<TSourceAction, TSourceTraits>;
    static constexpr auto reason = TTraits::validate_bind(^^source_io_type, ^^TKleisli);
    static_assert(
        reason.empty(), detail::error_msg<^^source_io_type, ^^TKleisli>(TTraits::name, reason));
    if constexpr (reason.empty())
    {
        using action_type = detail::BindAction<source_io_type, TKleisli>;
        return IO<action_type, TTraits>{{std::move(source), std::move(kleisli)}};
    }
    if constexpr (!reason.empty())
    {
        return detail::BindError{};
    }
}

/**
 * Binds a continuation (kleisli) to a source IO, naming the result.
 *
 * A function template, unlike class template argument deduction, may take its leading template
 * argument explicitly while deducing the source IO. Overloads on the first template argument: a
 * name (`char const *` non-type parameter) or a traits type.
 *
 * The result IO takes IOTraits<name_cstr>, which is also used to validate the source IO and
 * continuation pair, overriding the source IO's traits.
 *
 * See the defaulted-traits overload for the full contract, which is identical.
 *
 * @tparam name_cstr Null-terminated name with static storage duration (e.g. `"My bind"_ss`).
 * @tparam TSourceAction Source IO's action.
 * @tparam TSourceTraits Source IO's traits, used only for the source's own construction.
 * @tparam TKleisli Continuation taking the source's value and returning an IO.
 * @param source Source IO to run.
 * @param kleisli Continuation applied to the source's value.
 * @return IO running the source IO followed by the continuation's IO with IOTraits<name_cstr>,
 * or detail::BindError if the pair is invalid (which is also rejected by a friendly
 * static_assert).
 */
template <char const * name_cstr, class TSourceAction, class TSourceTraits, class TKleisli>
[[nodiscard]] constexpr auto bind(IO<TSourceAction, TSourceTraits> source, TKleisli kleisli)
{
    return bind<IOTraits<name_cstr>>(std::move(source), std::move(kleisli));
}

/**
 * Binds a continuation (kleisli) to a non-IO source, producing a compile-time error.
 *
 * This overload is selected when the source is not an IO type. It exists to provide a friendly
 * static_assert diagnostic message when bind is called with an invalid source type: the
 * static_assert always fails, so compilation never succeeds.
 *
 * Constrained to non-IO sources so that the IO overloads stay unambiguous when a traits argument
 * is passed explicitly (e.g. `bind<SomeTraits>(io, k)`).
 *
 * @tparam TTraits Traits used to validate the bind and generate the error message.
 * @tparam TNotAnIO Type of the invalid (non-IO) source argument.
 * @tparam TKleisli Continuation type.
 * @param source Invalid source argument (not an IO).
 * @param kleisli Continuation argument.
 * @return detail::BindError, like the other overloads, so the return type stays well-formed (e.g.
 * for deduction by `auto`); the static_assert always fails for a non-IO.
 */
template <class TTraits = IOTraits<>, class TNotAnIO, class TKleisli>
requires(!detail::is_io(^^TNotAnIO)) [[nodiscard]] constexpr auto bind(
    [[maybe_unused]] TNotAnIO source, [[maybe_unused]] TKleisli kleisli)
{
    static constexpr auto reason = TTraits::validate_bind(^^TNotAnIO, ^^TKleisli);
    static_assert(reason.empty(), detail::error_msg<^^TNotAnIO, ^^TKleisli>(TTraits::name, reason));
    static_assert(!reason.empty(), "io::bind error overload chosen without an error flagged");
    return detail::BindError{};
}
}  // namespace fnfelt::monad::io
