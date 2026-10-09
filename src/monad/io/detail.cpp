// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/monad/io/detail.hpp>

#include <doctest/doctest.h>

#include <array>
#include <tuple>
#include <utility>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/detail/create.hpp>

namespace
{
/// Incomplete type - has no meaningful invocation result.
struct Incomplete;

/// Async proxy deriving from the tag, exposing a `value_type` alias.
template <class TValue>
struct MockProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = TValue;
};

/// Base carrying an inherited `value_type` alias.
template <class TValue>
struct ProxyAliasBase
{
    using value_type = TValue;
};

/// Async proxy whose `value_type` alias is inherited.
struct InheritedProxy : ProxyAliasBase<double>, fnfelt::monad::io::AsyncProxyTag
{
};

/// Async proxy without a `value_type` alias.
struct MissingValueProxy : fnfelt::monad::io::AsyncProxyTag
{
};

/// Async proxy whose `value_type` alias is void.
struct VoidValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = void;
};

/// Only forward-declared, so a proxy's completeness guard can be exercised.
struct IncompleteProxy;

/// Action returning an async proxy.
struct AsyncAction
{
    constexpr MockProxy<int> operator()() const
    {
        return {};
    }
};

/// Action returning a proxy with an inherited `value_type` alias.
struct InheritedAsyncAction
{
    constexpr InheritedProxy operator()() const
    {
        return {};
    }
};

/// Action returning a tag-deriving proxy without a `value_type` alias.
struct MissingValueAction
{
    constexpr MissingValueProxy operator()() const
    {
        return {};
    }
};

/// Action declared to return an incomplete proxy; detection must be guarded, not hard-error.
struct IncompleteReturningAction
{
    IncompleteProxy operator()() const;
};

/// Empty class with no data members.
struct EmptyObject
{
};

/// Empty base for the inheritance walk.
struct EmptyBase
{
};

/// Class with a non-static data member.
struct NonEmptyObject
{
    int value = 0;
};

/// Derived class whose only state comes from its base.
struct DerivedFromNonEmpty : NonEmptyObject
{
};

/// Derived class whose base is empty.
struct DerivedFromEmpty : EmptyBase
{
};

/// Custom IO name for a custom-traits instantiation.
inline constexpr char custom_io_name[] = "CustomIO";
}  // namespace

TEST_CASE("detail::is_io recognises IO specialisations with any traits")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::CreateIOTraits;
    using fnfelt::monad::io::detail::is_io;

    using CustomIOTraits = fnfelt::monad::io::detail::CreateIOTraits<custom_io_name>;

    static_assert(is_io(^^IO<int (*)()>));
    static_assert(is_io(^^IO<int (*)(), CustomIOTraits>));
    static_assert(!is_io(^^int));
}

TEST_CASE("detail::action_value_meta reflects the action's result")
{
    using fnfelt::monad::io::detail::action_value_meta;

    static_assert(is_same_type(action_value_meta(^^int (*)()), ^^int));
    // Non-callable and incomplete actions have no meaningful result.
    static_assert(is_same_type(action_value_meta(^^int), ^^void));
    static_assert(is_same_type(action_value_meta(^^Incomplete), ^^void));
}

TEST_CASE("detail::io_action_meta reflects an IO's wrapped action")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::CreateIOTraits;
    using fnfelt::monad::io::detail::io_action_meta;

    using CustomIOTraits = fnfelt::monad::io::detail::CreateIOTraits<custom_io_name>;

    // The action reflection is exposed for IOs with any traits.
    static_assert(is_same_type(io_action_meta(^^IO<int (*)()>), ^^int (*)()));
    static_assert(is_same_type(io_action_meta(^^IO<int (*)(), CustomIOTraits>), ^^int (*)()));
    // Non-IO types have no wrapped action.
    static_assert(is_same_type(io_action_meta(^^int), ^^void));
}

TEST_CASE("detail::io_value_meta reflects an IO's action's result")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::CreateIOTraits;
    using fnfelt::monad::io::detail::io_value_meta;

    using CustomIOTraits = fnfelt::monad::io::detail::CreateIOTraits<custom_io_name>;

    static_assert(is_same_type(io_value_meta(^^IO<int (*)()>), ^^int));
    static_assert(is_same_type(io_value_meta(^^IO<int (*)(), CustomIOTraits>), ^^int));
    // Non-IO types have no value.
    static_assert(is_same_type(io_value_meta(^^int), ^^void));
}

TEST_CASE("detail::maybe_io_name names IOs and falls back for non-IOs")
{
    using fnfelt::monad::io::IO;
    using fnfelt::monad::io::detail::CreateIOTraits;
    using fnfelt::monad::io::detail::maybe_io_name;

    using CustomIOTraits = fnfelt::monad::io::detail::CreateIOTraits<custom_io_name>;

    static_assert(maybe_io_name<^^IO<int (*)()>>() == "IO");
    static_assert(maybe_io_name<^^IO<int (*)(), CustomIOTraits>>() == "CustomIO");
    static_assert(maybe_io_name<^^int>() == "<unknown>");
}

TEST_CASE("detail::action_value_meta reflects an async proxy's value_type alias")
{
    using fnfelt::monad::io::detail::action_value_meta;

    // A tag-deriving proxy's value comes from its `value_type` alias (directly or inherited).
    static_assert(is_same_type(action_value_meta(^^AsyncAction), ^^int));
    static_assert(is_same_type(action_value_meta(^^InheritedAsyncAction), ^^double));
    // A tag-deriving proxy without a value_type alias falls back to void.
    static_assert(is_same_type(action_value_meta(^^MissingValueAction), ^^void));
}

TEST_CASE("detail::action_is_async detects tag-deriving invocation results")
{
    using fnfelt::monad::io::detail::action_is_async;

    static_assert(action_is_async(^^AsyncAction));
    static_assert(action_is_async(^^InheritedAsyncAction));
    // Tag derivation is enough; a missing value_type alias does not affect async detection.
    static_assert(action_is_async(^^MissingValueAction));
    // Plain sync actions and non-class results are not async.
    static_assert(!action_is_async(^^decltype([] { return 1; })));
    static_assert(!action_is_async(^^int (*)()));
    // An incomplete invocation result is guarded, not a hard error.
    static_assert(!action_is_async(^^IncompleteReturningAction));
}

TEST_CASE("detail::async_proxy_has_value and value_meta walk bases for the value_type alias")
{
    using fnfelt::monad::io::detail::async_proxy_has_value;
    using fnfelt::monad::io::detail::async_proxy_value_meta;

    static_assert(async_proxy_has_value(^^MockProxy<int>));
    // The alias is inherited, so members_of alone would miss it; bases must be walked.
    static_assert(async_proxy_has_value(^^InheritedProxy));
    // A void alias is still an alias, so the proxy has a value (of type void).
    static_assert(async_proxy_has_value(^^VoidValueProxy));
    static_assert(!async_proxy_has_value(^^MissingValueProxy));
    static_assert(!async_proxy_has_value(^^IncompleteProxy));
    static_assert(!async_proxy_has_value(^^int));

    static_assert(is_same_type(async_proxy_value_meta(^^MockProxy<int>), ^^int));
    static_assert(is_same_type(async_proxy_value_meta(^^InheritedProxy), ^^double));
    static_assert(is_same_type(async_proxy_value_meta(^^VoidValueProxy), ^^void));
    static_assert(is_same_type(async_proxy_value_meta(^^MissingValueProxy), ^^void));
}

TEST_CASE("detail::is_empty_object recognises stateless types")
{
    using fnfelt::monad::io::detail::is_empty_object;

    static_assert(is_empty_object(^^EmptyObject));
    static_assert(is_empty_object(^^DerivedFromEmpty));
    static_assert(!is_empty_object(^^NonEmptyObject));
    // State inherited from a base still counts.
    static_assert(!is_empty_object(^^DerivedFromNonEmpty));
    // Non-class and incomplete types have no state to speak of, but are not empty objects.
    static_assert(!is_empty_object(^^int));
    static_assert(!is_empty_object(^^void));
    static_assert(!is_empty_object(^^Incomplete));
    // Non-capturing lambdas are empty closures.
    static_assert(is_empty_object(^^decltype([](int x) { return x; })));
    // Stateless coroutine lambdas are the async-function convention.
    static_assert(is_empty_object(^^decltype([](auto, int x) -> lf::task<int> { co_return x; })));
}

TEST_CASE("detail::is_empty_object rejects capturing closures")
{
    using fnfelt::monad::io::detail::is_empty_object;

    int const captured = 1;
    auto const capturing = [captured] { return captured; };
    static_assert(!is_empty_object(^^decltype(capturing)));
}

#endif  // DOCTEST_CONFIG_DISABLE
