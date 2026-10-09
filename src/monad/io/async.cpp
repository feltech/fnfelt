// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/monad/io/IO.hpp>
#include <fnfelt/monad/io/async.hpp>

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
using fnfelt::monad::io::IO;
using fnfelt::monad::io::detail::CreateIOTraits;
/// Scheduler stand-in, accepted-and-ignored on the synchronous path.
struct MockPool
{
};

/// Stateless coroutine entry for the executable test proxies; produces its stored argument.
template <class TValue>
struct EchoFn
{
    auto operator()(auto /*self*/, TValue value) const -> lf::task<TValue>
    {
        co_return value;
    }
};

/// Async proxy deriving from the tag, exposing a `value_type` alias and the executable protocol.
template <class TValue>
struct MockProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = TValue;
    std::tuple<TValue> arg;
    static constexpr EchoFn<TValue> fn{};
};

/// Base carrying an inherited `value_type` alias.
template <class TValue>
struct ProxyAliasBase
{
    using value_type = TValue;
};

/// Async proxy whose `value_type` alias is inherited, with the executable protocol.
struct InheritedProxy : ProxyAliasBase<double>, fnfelt::monad::io::AsyncProxyTag
{
    std::tuple<double> arg;
    static constexpr EchoFn<double> fn{};
};

/// A closure-valued alias hiding a different inherited value type.
struct ClosureAliasProxy : ProxyAliasBase<int>, fnfelt::monad::io::AsyncProxyTag
{
    using value_type = decltype([value = 42] { return value; });
};

static_assert(fnfelt::monad::io::detail::async_proxy_has_value(^^ClosureAliasProxy));
static_assert(!fnfelt::monad::io::detail::async_proxy_ambiguous_value(^^ClosureAliasProxy));
static_assert(is_same_type(
    fnfelt::monad::io::detail::async_proxy_value_meta(^^ClosureAliasProxy),
    ^^ClosureAliasProxy::value_type));

/// Async proxy without a `value_type` alias.
struct MissingValueProxy : fnfelt::monad::io::AsyncProxyTag
{
};

/// Async proxy whose `value_type` alias is void.
struct VoidValueProxy : fnfelt::monad::io::AsyncProxyTag
{
    using value_type = void;
};

/// Action returning a valid async proxy.
struct AsyncIntAction
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

/// Action returning a proxy without a `value_type` alias.
struct MissingValueAction
{
    constexpr MissingValueProxy operator()() const
    {
        return {};
    }
};

/// Action returning a proxy whose `value_type` alias is void.
struct VoidValueAction
{
    constexpr VoidValueProxy operator()() const
    {
        return {};
    }
};

/// Plain callable for the function IO of an async-transparent ap chain.
struct Increment
{
    int operator()(int value) const
    {
        return value + 1;
    }
};

/// Action returning an async proxy of a callable, for ap compatibility checks.
struct AsyncFnAction
{
    constexpr MockProxy<Increment> operator()() const
    {
        return {};
    }
};

using async_int_io = IO<AsyncIntAction>;
using async_fn_io = IO<AsyncFnAction>;

/// Action counting move and copy constructions, for the by-value proxy checks.
struct CopyTrackingAction
{
    static inline int moves = 0;
    static inline int copies = 0;
    int value = 1;

    CopyTrackingAction() = default;
    CopyTrackingAction(CopyTrackingAction const & other) : value{other.value}
    {
        ++copies;
    }
    CopyTrackingAction(CopyTrackingAction && other) noexcept : value{other.value}
    {
        ++moves;
    }
    CopyTrackingAction & operator=(CopyTrackingAction const &) = delete;
    CopyTrackingAction & operator=(CopyTrackingAction &&) = delete;
    int operator()() const
    {
        return value;
    }
};

/// Copy-tracking argument stored by an async leaf.
struct CopyTrackingArg
{
    static inline int copies = 0;
    static inline int moves = 0;
    int value = 1;

    CopyTrackingArg() = default;
    CopyTrackingArg(CopyTrackingArg const & other) : value{other.value}
    {
        ++copies;
    }
    CopyTrackingArg(CopyTrackingArg && other) noexcept : value{other.value}
    {
        ++moves;
    }
    CopyTrackingArg & operator=(CopyTrackingArg const &) = delete;
    CopyTrackingArg & operator=(CopyTrackingArg &&) = delete;
};

/// Custom execution policy allowing only lvalue run proxies, proving the validator is consulted.
struct LvalueRunTraits : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static consteval std::string_view validate_sync_wait(
        std::meta::info self_meta,
        std::meta::info /*action_meta*/,
        std::vector<std::meta::info> const & /*scheduler_metas*/)
    {
        return is_lvalue_reference_type(self_meta) ? std::string_view{} : "expected lvalue proxy";
    }
};

}  // namespace

TEST_CASE("CreateIOTraits validates run proxy consumption and async scheduler arguments")
{
    using sync_action = int (*)();
    using sync_proxy = fnfelt::monad::io::IORunProxy<sync_action>;
    using async_proxy = fnfelt::monad::io::IORunProxy<AsyncIntAction>;

    static_assert(fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
                      ^^sync_proxy &&, ^^sync_action, {})
                      .empty());
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
            ^^sync_proxy &, ^^sync_action, {}) ==
        "sync_wait must be called on an rvalue run proxy: the stored callable is invoked "
        "as an rvalue. Consume the proxy, e.g. std::move(proxy).sync_wait()");
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
            ^^sync_proxy const &, ^^sync_action, {}) ==
        "sync_wait must be called on an rvalue run proxy: the stored callable is invoked "
        "as an rvalue. Consume the proxy, e.g. std::move(proxy).sync_wait()");
    static_assert(fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
                      ^^sync_proxy &&, ^^sync_action, {^^MockPool, ^^int})
                      .empty());
    static_assert(fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
                      ^^async_proxy &&, ^^AsyncIntAction, {})
                      .empty());
    static_assert(fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
                      ^^async_proxy &&, ^^AsyncIntAction, {^^lf::lazy_pool & })
                      .empty());
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
            ^^async_proxy &&, ^^AsyncIntAction, {^^MockPool}) ==
        "sync_wait scheduler argument must satisfy libfork's scheduler concept");
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_sync_wait(
            ^^async_proxy &&, ^^AsyncIntAction, {^^lf::lazy_pool, ^^lf::lazy_pool}) ==
        "sync_wait accepts at most one scheduler argument");
}

TEST_CASE("async proxies are detected by reflection, not run")
{
    using fnfelt::monad::io::detail::action_is_async;
    using fnfelt::monad::io::detail::action_value_meta;
    using fnfelt::monad::io::detail::async_proxy_has_value;
    using fnfelt::monad::io::detail::async_proxy_value_meta;

    static_assert(action_is_async(^^AsyncIntAction));
    static_assert(action_is_async(^^InheritedAsyncAction));
    static_assert(!action_is_async(^^decltype([] { return 1; })));

    static_assert(is_same_type(action_value_meta(^^AsyncIntAction), ^^int));
    static_assert(is_same_type(action_value_meta(^^InheritedAsyncAction), ^^double));

    static_assert(std::is_same_v<async_int_io::value_type, int>);
    static_assert(std::is_same_v<IO<InheritedAsyncAction>::value_type, double>);

    static_assert(async_proxy_has_value(^^InheritedProxy));
    static_assert(is_same_type(async_proxy_value_meta(^^InheritedProxy), ^^double));
    static_assert(!async_proxy_has_value(^^MissingValueProxy));
    static_assert(is_same_type(async_proxy_value_meta(^^MissingValueProxy), ^^void));
    static_assert(async_proxy_has_value(^^VoidValueProxy));
    static_assert(is_same_type(async_proxy_value_meta(^^VoidValueProxy), ^^void));

    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(^^AsyncIntAction).empty());
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(^^MissingValueAction) ==
        "provided action's async proxy does not expose a value_type alias");
    static_assert(
        fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(^^VoidValueAction) ==
        "provided action's async proxy must not produce void");
}

TEST_CASE("IORunProxy executes a custom async proxy")
{
    CHECK_EQ(IO<AsyncIntAction>{AsyncIntAction{}}.build().sync_wait(), 0);
    CHECK_EQ(IO<AsyncIntAction>{AsyncIntAction{}}.build().sync_wait(lf::lazy_pool{2}), 0);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
