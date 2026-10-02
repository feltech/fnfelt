// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file type_traits.hpp
 *
 * Type traits.
 *
 * Argument validation uses C++26 reflection so that misuse is reported early with a
 * human-readable static_assert message, rather than via concepts, type traits, fallback
 * specialisations or SFINAE tricks. Each validator is a separate structural non-type template
 * parameter so that a stub can be injected in tests, reducing the number of compile-fail tests
 * required.
 */
#pragma once

#include <meta>

#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

#include <fnfelt/detail/errors.hpp>

#include <fnfelt/macros_push.hpp>

namespace fnfelt
{
namespace detail
{
/**
 * Placeholder class template with type-only template parameters, for use on validation failure.
 *
 * Its only role is to give the failed splice attempt something harmless to substitute into, so a
 * failed validation produces exactly one readable diagnostic instead of a cascade of hard errors
 * at the splice site.
 */
template <class...>
struct ErrorPlaceholderTemplate
{
};

/**
 * True when every template argument of the reflected specialization is a type.
 *
 * The arguments are iterated with a plain range-for over the unbound template_arguments_of result
 * (not bound to a constexpr variable and not expanded with template for) - both are broken on
 * gcc-16. Folded into a single predicate because a static_assert on a loop variable is ill-formed.
 *
 * Only valid for a reflected specialization, i.e. the caller must first establish
 * has_template_arguments(target_meta). Calling it on an unbound template or non-template throws a
 * std::meta::exception, which in a constant expression surfaces as a non-constant-condition error.
 *
 * @param target_meta Reflection of the specialization to test.
 * @return True when target_meta has no non-type template parameters.
 */
consteval bool all_template_arguments_are_types(std::meta::info target_meta)
{
    for (auto const arg : template_arguments_of(target_meta))
    {
        if (!is_type(arg))
        {
            return false;
        }
    }
    return true;
}

/**
 * Validates that a reflected type is a specialization of a type-parameter-only class template.
 *
 * Implemented as a structural functor (not a free function) because a consteval function cannot
 * be captured as a non-type template parameter and forwarded: that would form a pointer to an
 * immediate function, which gcc-16 rejects in a constant expression.
 *
 * Preconditions are checked in order and the first failure is named, so the diagnostic is both
 * specific and stable. The has_template_arguments and is_class_template checks guard the later
 * template_of and all_template_arguments_are_types calls, which would otherwise throw for an
 * unbound template or non-template.
 */
struct ValidateTemplateOf
{
    /**
     * Names the first precondition that candidate fails, or empty when it passes.
     *
     * @param candidate Reflection of the type to validate.
     * @return Empty view when candidate is a type-parameter-only class template specialization,
     * otherwise a short description of the first failing precondition.
     */
    consteval std::string_view operator()(std::meta::info candidate) const
    {
        // The is_type check is unreachable via Unspecialise<TContainer> (TContainer is always a
        // type), but makes the helper safe to call directly and guards is_const_type etc. on
        // non-type reflections.
        if (!is_type(candidate))
        {
            return "is not a type";
        }
        if (is_const_type(candidate) || is_volatile_type(candidate))
        {
            return "is cv-qualified";
        }
        if (is_reference_type(candidate))
        {
            return "is a reference type";
        }
        if (is_pointer_type(candidate))
        {
            return "is a pointer type";
        }
        if (!has_template_arguments(candidate))
        {
            return "is not a template";
        }
        if (!is_class_template(template_of(candidate)))
        {
            return "is not a class template";
        }
        if (!all_template_arguments_are_types(candidate))
        {
            return "has a non-type template parameter";
        }
        return {};
    }
};

/**
 * Instance of ValidateTemplateOf, usable as validate_template_of(candidate).
 */
inline constexpr ValidateTemplateOf validate_template_of{};

/**
 * Extracts the unbound class template from a container specialization reflection.
 *
 * The reflection is passed as a non-type template parameter because a std::meta::info function
 * parameter cannot be used in a constant expression in gcc-16. Validation is collapsed to a single
 * static_assert in the owning class, keyed on the validate predicate, so the user sees one specific
 * message naming the first failing precondition. The if constexpr guard then keeps a failed
 * validation to exactly one diagnostic: gcc-16's error recovery evaluates later same-level
 * statements after a fired assert but skips discarded branches, so the throwing template_of call in
 * the return statement is guarded by the (constant) success check.
 *
 * @tparam target_meta Reflection of the container specialization to unspecialise.
 * @tparam validator Validator function of signature string_view(meta::info) to check the container
 * specialization, returning empty message if valid, or error description if invalid.
 * @return Reflection of the unbound class template, or ErrorPlaceholderTemplate on validation
 * failure.
 */
template <std::meta::info target_meta, auto validator>
consteval std::meta::info template_of_if_valid()
{
    if constexpr (!validator(target_meta).empty())
    {
        return ^^ErrorPlaceholderTemplate;
    }
    return template_of(target_meta);
}
}  // namespace detail

/**
 * Extracts the template and type arguments from a container specialization.
 *
 * Validation and the splice are both guarded: instantiating this primary on an unsupported type
 * triggers exactly one readable static_assert, and detail::type_specialisation delegates to the
 * same validator rather than instantiating anything separate.
 *
 * @tparam TContainer Container specialization to unspecialise.
 * @tparam validator Validator function of signature string_view(meta::info) to check the container
 * specialization, returning empty message if valid, or error description if invalid.
 */
template <class TContainer, auto validator = detail::validate_template_of>
struct Unspecialise
{
    /// Validation
    consteval
    {
        constexpr auto reason = validator(^^TContainer);
        static_assert(
            reason.empty(),
            detail::construct_type_error_msg(
                ^^TContainer,
                "Unspecialise",
                "requires a class template specialization with only type parameters: ",
                reason));
    }

    /**
     * Alias rebinding this container family to new type arguments.
     *
     * Deliberately unconstrained so that direct misuse of Unspecialise triggers the reflective
     * diagnostics in detail::template_of_if_valid.
     *
     * @tparam TArgs Type arguments to specialize TContainer's template with.
     */
    template <class... TArgs>
    using specialise = [:detail::template_of_if_valid<^^TContainer, validator>():]<TArgs...>;
};

namespace detail
{
/**
 * Validates that a reflected type is a rebindable class template specialization and that the
 * requested replacement type arguments can specialize it.
 *
 * Delegates the container-shape preconditions to ValidateTemplateOf, then checks substitution of
 * the replacement arguments with can_substitute. Implemented as a structural functor for the same
 * reason as ValidateTemplateOf.
 *
 * The container-shape delegation is load-bearing: can_substitute expects a template reflection, so
 * template_of must only be called once validation has established that container_meta is a class
 * template specialization. can_substitute also throws for a null or kind-mismatched argument
 * reflection; that cannot occur here because arg_metas always reflect the type pack TArgs.
 */
struct ValidateRebind
{
    /**
     * Names the first precondition that the rebind fails, or empty when it passes.
     *
     * @param container_meta Reflection of the container specialization to validate.
     * @param arg_metas Reflections of the replacement type arguments.
     * @return Empty view when the container is a type-parameter-only class template
     * specialization that can be specialized with arg_metas, otherwise a short description of the
     * first failing precondition.
     */
    consteval std::string_view operator()(
        std::meta::info container_meta, std::initializer_list<std::meta::info> arg_metas) const
    {
        if (auto const reason = validate_template_of(container_meta); !reason.empty())
        {
            return reason;
        }
        if (!can_substitute(template_of(container_meta), arg_metas))
        {
            return "cannot be specialized with the requested type arguments";
        }
        return {};
    }
};

/**
 * Instance of ValidateRebind, usable as validate_rebind(candidate, arg_metas).
 */
inline constexpr ValidateRebind validate_rebind{};

/**
 * Extracts the unbound class template from a rebindable container specialization reflection.
 *
 * Rebind counterpart of template_of_if_valid: the validator additionally sees the replacement
 * argument reflections, so the same guarded-splice shape yields exactly one diagnostic on failure.
 *
 * @tparam container_meta Reflection of the container specialization to rebind.
 * @tparam validator Validator function of signature string_view(meta::info,
 * initializer_list<meta::info>) to check the container and replacement arguments, returning empty
 * message if valid, or error description if invalid.
 * @tparam arg_metas Reflections of the replacement type arguments.
 * @return Reflection of the unbound class template, or ErrorPlaceholderTemplate on validation
 * failure.
 */
template <std::meta::info container_meta, auto validator, std::meta::info... arg_metas>
consteval std::meta::info template_of_if_rebindable()
{
    if constexpr (!validator(container_meta, {arg_metas...}).empty())
    {
        return ^^ErrorPlaceholderTemplate;
    }
    return template_of(container_meta);
}

/**
 * Concept satisfied when TContainer is a specialization of a template taking only type parameters.
 *
 * Named form of the reflection predicate ValidateTemplateOf, retained as a SFINAE-safe detection
 * surface (a static_assert cannot be used in a requires-clause, partial specialization or concept).
 * Single source of truth: it delegates to the same validator used by Unspecialise and Rebind.
 *
 * @tparam TContainer Type to test.
 */
template <class TContainer>
concept type_specialisation = validate_template_of(^^TContainer).empty();

/**
 * Concept satisfied when TContainer supports rebinding to TArgs.
 *
 * Named form of the reflection predicate ValidateRebind, retained as a SFINAE-safe detection
 * surface. Single source of truth: it delegates to the same validator used by Rebind.
 *
 * @tparam TContainer Container type to rebind.
 * @tparam TArgs Type arguments to rebind TContainer to.
 */
template <class TContainer, class... TArgs>
concept rebindable = validate_rebind(^^TContainer, {^^TArgs...}).empty();

/**
 * Rebinds TContainer to TArgs.
 *
 * Single primary template - no constrained partial specialization and no fallback. A consteval
 * block validates the container and replacement arguments up front and fires exactly one readable
 * static_assert naming the first failing precondition, then type splices the unbound class template
 * back together with TArgs. On validation failure the splice yields
 * ErrorPlaceholderTemplate<TArgs...> instead of hard-erroring, so the user sees one diagnostic.
 *
 * The validator is a non-type template parameter so a stub can be injected in tests. It is
 * deliberately positioned after TContainer but before the TArgs pack: a defaulted value parameter
 * before a trailing pack is specifiable, whereas validator-last cannot be defaulted. This is why
 * the rebind_t alias passes detail::validate_rebind explicitly.
 *
 * @tparam TContainer Container specialization to rebind.
 * @tparam validator Validator function of signature string_view(meta::info,
 * initializer_list<meta::info>) to check the container and replacement arguments, returning empty
 * message if valid, or error description if invalid.
 * @tparam TArgs Type arguments to specialize the container template with.
 */
template <class TContainer, auto validator = validate_rebind, class... TArgs>
struct Rebind
{
    /// Validation
    consteval
    {
        constexpr auto reason = validator(^^TContainer, {^^TArgs...});
        static_assert(
            reason.empty(),
            construct_type_error_msg(^^TContainer, "rebind_t", "cannot rebind: ", reason));
    }

    /// The rebound container type, or ErrorPlaceholderTemplate<TArgs...> on validation failure.
    using type = [:template_of_if_rebindable<^^TContainer, validator, ^^TArgs...>():]<TArgs...>;
};
}  // namespace detail

/**
 * Rebinds TContainer to new element types.
 *
 * If TContainer is a template specialization that supports rebind, e.g. std::vector<int>, yields
 * the same template specialized with TArgs, e.g. std::vector<double>. Otherwise triggers a
 * readable static_assert error message.
 *
 * @par Rebind semantics
 * Rebinding replaces ALL template type arguments of the container - any remaining (non-type)
 * arguments are not preserved. In particular a custom allocator is discarded and the family
 * default is restored, e.g. rebind_t<std::vector<int, MyAlloc<int>>, double> is
 * std::vector<double> (default std::allocator), not std::vector<double, MyAlloc<double>>.
 *
 * Containers with non-type template parameters, e.g. std::array<T, N>, are not rebindable at
 * all - they produce the friendly static_assert error.
 *
 * @tparam TContainer Container specialization to rebind.
 * @tparam TArgs Type arguments to specialize the container template with.
 */
template <class TContainer, class... TArgs>
using rebind_t = detail::Rebind<TContainer, detail::validate_rebind, TArgs...>::type;
}  // namespace fnfelt

#include <fnfelt/macros_pop.hpp>
