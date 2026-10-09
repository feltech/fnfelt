// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
// IWYU pragma: private, include "../../io.hpp"
/**
 * @file monad/io/detail/create.hpp
 *
 * Implementation details for constructing IO values and validating an IO's action, including the
 * executable shape of any asynchronous proxy an action produces.
 */
#pragma once

#include <meta>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/core/scheduler.hpp>
#include <libfork/core/task.hpp>

#include <fnfelt/detail/callable_shape.hpp>
#include <fnfelt/detail/type_names.hpp>
#include <fnfelt/monad/io/detail.hpp>
#include <fnfelt/monad/io/detail/fwd.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt::monad::io
{
namespace detail
{
/**
 * Move a value through libfork without requiring assignment of the value itself.
 *
 * This is a workaround for libfork requiring more than it actually needs.
 *
 * libfork requires co_returned types to be move-assignable, even though move assignment may not be
 * used. So if an IO returns a move-constructible type that is not move-assignable (e.g. a capturing
 * lambda) then we must wrap it in a move-assignable box.
 *
 * Note that since move assignment is not actually used, attempted move assignment of a box results
 * in a compile-time error.
 *
 * Whether a box is needed and used is detected at compile time. See @ref task_result_t.
 *
 * @tparam T Move-constructible logical value.
 */
template <class T>
struct ResultBox
{
    /// Owned logical value, always present.
    T value;

    /// A box must be constructed with a logical value.
    ResultBox() = delete;

    /**
     * Retain an owned logical value.
     *
     * @param produced Value to retain.
     */
    constexpr explicit ResultBox(T produced) : value(std::move(produced)) {}
    /// Transfer ownership of the logical value.
    constexpr ResultBox(ResultBox &&) = default;

    /**
     * Reject unexpected assignment of a boxed result.
     *
     * @return This box.
     */
    constexpr ResultBox & operator=(ResultBox &&) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        // Temporary workaround for libfork requiring std::movable even though the result slots
        // used by IO construct values in empty storage. Satisfy its assignment check, but fail
        // compilation if a result path ever actually requires assignment.
        static_assert(
            false,
            "fnfelt: ResultBox move assignment is unsupported; IO results must be "
            "move-constructed");
        return *this;
    }
};

/// Preserve ordinary task results, boxing only values libfork cannot assign.
template <class T>
using task_result_t = std::conditional_t<lf::returnable<T>, T, ResultBox<T>>;

/**
 * Adapt a logical result to libfork's movable transport.
 *
 * @param value Completed logical result.
 * @return Owned runtime transport.
 */
template <class T>
constexpr task_result_t<T> pack_result(T value)
{
    return task_result_t<T>(std::move(value));
}

/**
 * Consume an ordinary task result.
 *
 * @param value Completed result.
 * @return Owned logical value.
 */
template <class T>
constexpr T unpack_result(T value)
{
    return value;
}

/**
 * Consume a boxed task result.
 *
 * @param boxed Completed result.
 * @return Owned logical value, never a reference into the box.
 */
template <class T>
constexpr T unpack_result(ResultBox<T> boxed)
{
    return std::move(boxed.value);
}

/**
 * Access a completed result without consuming it.
 *
 * @param value Completed result.
 * @return Logical value retained by its result slot.
 */
template <class T>
constexpr T & result_ref(T & value)
{
    return value;
}

/**
 * Access a boxed result without consuming it.
 *
 * @param boxed Completed result.
 * @return Logical value retained by its result slot.
 */
template <class T>
constexpr T & result_ref(ResultBox<T> & boxed)
{
    return boxed.value;
}

/**
 * Reflect the runtime transport for a logical value.
 *
 * @param value_meta Logical value type.
 * @return Type accepted by libfork's movable result contract.
 */
consteval std::meta::info task_result_meta(std::meta::info value_meta)
{
    if (can_substitute(^^lf::task, {value_meta}))
    {
        return value_meta;
    }
    return substitute(^^ResultBox, {value_meta});
}

/// Placeholder for a rejected build.
struct BuildError
{
};

/**
 * Format an invalid build.
 *
 * @param name Diagnostic name.
 * @param reason Validation failure.
 * @return Diagnostic string.
 */
consteval std::string build_error_msg(std::string_view name, std::string_view reason)
{
    std::string msg{"fnfelt: "};
    msg += name;
    msg += " build is invalid: ";
    msg += reason;
    return msg;
}

/**
 * Substitution target for checking a reflected scheduler type.
 *
 * @tparam TScheduler Scheduler type to check.
 */
template <class TScheduler>
requires lf::scheduler<TScheduler> struct SchedulerArg
{
};

/**
 * Construct a diagnostic for a rejected run-proxy consumption.
 *
 * @tparam action_meta Reflection of the action type.
 * @param io_name Display name of the IO.
 * @param reason Validation failure reason.
 * @return Formatted error message string.
 */
template <std::meta::info action_meta>
consteval std::string sync_wait_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg = "fnfelt: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += " sync_wait is invalid: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(action_meta));
    return msg;
}

/**
 * Constructs the diagnostic message for a rejected action.
 *
 * The message includes the IO name, the specific validation failure reason, and the offending
 * action's display string, so the reason stays readable before any compiler truncation of the
 * message.
 *
 * This function is called only within a static_assert to provide friendly diagnostics; it must
 * never be evaluated at runtime.
 *
 * @tparam action_meta Reflection of the (decayed) action type.
 * @param io_name Display name of the resulting IO (from traits).
 * @param reason Validation failure reason from `TTraits::validate_action`.
 * @return Formatted error message string.
 */
template <std::meta::info action_meta>
consteval std::string create_error_msg(std::string_view io_name, std::string_view reason)
{
    std::string msg;
    msg += "fnfelt: ";
    fnfelt::detail::append_string_view(msg, io_name);
    msg += ' ';
    msg += "action is invalid: ";
    fnfelt::detail::append_string_view(msg, reason);
    msg += ": ";
    fnfelt::detail::append_string_view(msg, display_string_of(action_meta));
    return msg;
}

/**
 * Traits validating an IO's action and the executable shape of any asynchronous proxy it produces.
 *
 * @tparam name_cstr Null-terminated name of the IO instantiation, used in diagnostics. Must have
 * static storage duration.
 */
template <char const * name_cstr>
struct CreateIOTraits
{
    /// Name of this IO instantiation, used in compile-time diagnostic messages.
    // A string_view over a static string is constant-initialised and cannot throw.
    // NOLINTNEXTLINE(*-statically-constructed-objects)
    static constexpr std::string_view name{name_cstr};

    /**
     * Validate construction of the stored action from a build receiver.
     *
     * @param action_meta Exact cv-ref-qualified action expression type.
     * @return Empty on success, otherwise a rejection reason.
     */
    static consteval std::string_view validate_build(std::meta::info action_meta)
    {
        if (!is_type(action_meta) || !is_complete_type(remove_cvref(action_meta)))
        {
            return "build requires a complete action type";
        }
        if (!is_constructible_type(remove_cvref(action_meta), {action_meta}))
        {
            return "stored action cannot be copied or moved from the build receiver";
        }
        return {};
    }

    /**
     * Check whether a type is a (cv-ref-stripped) specialisation of IO, with any traits.
     *
     * @param type_meta Reflection of the type.
     * @return True if the type is an IO specialisation.
     */
    static consteval bool is_io(std::meta::info type_meta)
    {
        return detail::is_io(type_meta);
    }

    /**
     * Validate consumption of a run proxy and its scheduler arguments.
     *
     * Synchronous actions accept and ignore any scheduler arguments. Asynchronous actions accept
     * no scheduler or one libfork scheduler.
     *
     * @param self_meta Reflection of the run proxy's cv-ref-qualified type.
     * @param action_meta Reflection of the stored action type.
     * @param scheduler_metas Reflections of the scheduler argument types.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_sync_wait(
        std::meta::info self_meta,
        std::meta::info action_meta,
        std::vector<std::meta::info> const & scheduler_metas)
    {
        if (is_lvalue_reference_type(self_meta))
        {
            return "sync_wait must be called on an rvalue run proxy: the stored callable is "
                   "invoked "
                   "as an rvalue. Consume the proxy, e.g. std::move(proxy).sync_wait()";
        }
        if (detail::action_is_async(action_meta) && !scheduler_metas.empty())
        {
            if (scheduler_metas.size() > 1)
            {
                return "sync_wait accepts at most one scheduler argument";
            }
            std::meta::info const scheduler_meta = remove_cvref(scheduler_metas[0]);
            if (!is_complete_type(scheduler_meta) ||
                !can_substitute(^^detail::SchedulerArg, {scheduler_meta}))
            {
                return "sync_wait scheduler argument must satisfy libfork's scheduler concept";
            }
        }
        return {};
    }

    /**
     * Validate the executable shape of an asynchronous proxy.
     *
     * Detection as asynchronous (deriving @ref AsyncProxyTag with a non-void `value_type` alias) is
     * not enough to run a proxy: execution additionally requires the stored arguments and the
     * stateless coroutine entry. Acceptance therefore checks that the proxy:
     *
     * - exposes an unambiguous non-void `value_type` alias;
     * - stores its arguments in a non-static data member named `arg` whose type is a `std::tuple`
     *   specialisation, whose elements are spread into `fn` at run time;
     * - exposes a static data member named `fn`, callable as `fn(self, args...)` with a
     *   libfork-generated first argument, returning an `lf::task` whose value matches `value_type`.
     *
     * The unwrapped value must also satisfy the plain-result rules: it must not be a reference or
     * an IO, and it must be move constructible. Internal composition proxies support nonassignable
     * logical values; other proxies must produce values satisfying libfork's movable contract.
     *
     * @param proxy_meta Reflection of the async proxy type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_async_proxy(std::meta::info proxy_meta)
    {
        proxy_meta = dealias(remove_cvref(proxy_meta));
        if (!detail::async_proxy_has_value(proxy_meta))
        {
            return "provided action's async proxy does not expose a value_type alias";
        }
        if (detail::async_proxy_ambiguous_value(proxy_meta))
        {
            return "provided action's async proxy exposes ambiguous value_type aliases";
        }
        std::meta::info const value_meta = detail::async_proxy_value_meta(proxy_meta);
        if (is_void_type(value_meta))
        {
            return "provided action's async proxy must not produce void";
        }
        if (is_reference_type(value_meta))
        {
            return "provided action's async proxy value must not be a reference type";
        }
        if (detail::is_io(value_meta))
        {
            return "provided action's async proxy value should not be an IO";
        }
        if (!is_complete_type(value_meta))
        {
            return "provided action's async proxy value is not a complete type";
        }
        bool const has_transport = is_base_of_type(^^TransportProxyTag, proxy_meta);
        if (!is_move_constructible_type(value_meta) ||
            (!has_transport && !is_move_assignable_type(value_meta)))
        {
            return "provided action's async proxy value must be movable";
        }
        // The stored argument tuple: a non-static data member named "arg".
        std::meta::info arg_type = ^^void;
        for (std::meta::info const member :
             nonstatic_data_members_of(proxy_meta, std::meta::access_context::unprivileged()))
        {
            if (has_identifier(member) && identifier_of(member) == "arg")
            {
                arg_type = dealias(type_of(member));
            }
        }
        if (is_void_type(arg_type))
        {
            return "provided action's async proxy must expose an arg member";
        }
        if (!has_template_arguments(arg_type) || template_of(arg_type) != ^^std::tuple)
        {
            return "provided action's async proxy arg member must be a std::tuple";
        }
        // The stateless coroutine entry: a static data member named "fn".
        std::meta::info fn_type = ^^void;
        for (std::meta::info const member :
             static_data_members_of(proxy_meta, std::meta::access_context::unprivileged()))
        {
            if (has_identifier(member) && identifier_of(member) == "fn")
            {
                fn_type = remove_cvref(type_of(member));
            }
        }
        if (is_void_type(fn_type))
        {
            return "provided action's async proxy must expose a static fn member";
        }
        // libfork injects a `self` first argument, so a placeholder stands in for it, followed by
        // the stored tuple's element types.
        std::vector<std::meta::info> args{^^int};
        for (std::meta::info const element : template_arguments_of(arg_type))
        {
            args.push_back(element);
        }
        if (!is_complete_type(fn_type) || !is_invocable_type(fn_type, args))
        {
            return "provided action's async proxy fn must accept the stored arguments";
        }
        std::meta::info const result = dealias(invoke_result(fn_type, args));
        // `template_of` comparison is the same idiom as `is_io`; `lf::task` is an inline-namespace
        // name.
        if (!has_template_arguments(result) || template_of(result) != ^^lf::task)
        {
            return "provided action's async proxy fn must return an lf::task";
        }
        auto const task_value_meta = has_transport ? task_result_meta(value_meta) : value_meta;
        if (!is_same_type(template_arguments_of(result)[0], task_value_meta))
        {
            return "provided action's async proxy fn result does not match its value_type";
        }
        return {};
    }

    /**
     * Validate the action given to an IO.
     *
     * @param action_meta Reflection of the action type.
     * @return Empty string if validation passes, otherwise a string describing the error.
     */
    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        action_meta = dealias(action_meta);
        // An IO is rejected before the shape checks, so an IO over any action (including one that
        // would fail the shape checks alone) is rejected as an IO first.
        if (detail::is_io(action_meta))
        {
            return "provided action is already an IO, pass its action instead";
        }
        switch (fnfelt::detail::callable_shape_of(action_meta))
        {
            case fnfelt::detail::CallableShape::incomplete:
                return "provided action is not a complete type";
            case fnfelt::detail::CallableShape::reference:
                return "provided action must not be a reference type";
            case fnfelt::detail::CallableShape::not_class_or_function_pointer:
                return "provided action is not a class or function pointer type";
            case fnfelt::detail::CallableShape::not_movable:
                return "provided action must be move constructible";
            case fnfelt::detail::CallableShape::valid:
                break;
        }
        if (!is_invocable_type(action_meta, {}))
        {
            return "provided action is not callable with no arguments";
        }
        std::meta::info const result_meta = invoke_result(action_meta, {});
        if (is_reference_type(result_meta))
        {
            return "provided action returns a reference, return by value instead";
        }
        if (detail::action_is_async(action_meta))
        {
            return validate_async_proxy(remove_cvref(result_meta));
        }
        if (is_void_type(result_meta))
        {
            return "provided action does not return a value";
        }
        if (detail::is_io(result_meta))
        {
            return "provided action should not return an IO";
        }
        if (!is_move_constructible_type(result_meta))
        {
            return "provided action returns a non-movable type";
        }
        return {};
    }
};
}  // namespace detail
}  // namespace fnfelt::monad::io

#include <fnfelt/macros_pop.hpp>
