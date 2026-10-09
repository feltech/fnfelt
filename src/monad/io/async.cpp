// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <meta>

#include <type_traits>
#include <utility>

#include <fnfelt/monad/io.hpp>

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
using fnfelt::monad::io::IO;
using fnfelt::monad::io::IOTraits;

/// Scheduler stand-in, accepted-and-ignored on the synchronous path.
struct MockPool
{
};

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

/// Async proxy whose `value_type` alias is inherited from a base.
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

/// Constant-evaluated multi-hop pipeline: (1 + 1) * 2 + 3 == 7.
consteval int constexpr_multi_hop()
{
    constexpr auto pipeline =
        fnfelt::monad::io::create([] { return 1; })
            .transform([](int x) { return x + 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 2; }); })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 3; }); });
    return pipeline().sync_wait();
}
}  // namespace

TEST_CASE("RunProxy runs sync actions from rvalue, lvalue and const-lvalue IOs")
{
    auto io = IO{[] { return 1; }};
    IO const const_io{[] { return 2; }};

    CHECK_EQ(IO{[] { return 0; }}().sync_wait(), 0);
    CHECK_EQ(io().sync_wait(), 1);
    CHECK_EQ(const_io().sync_wait(), 2);
}

TEST_CASE("RunProxy accepts and ignores scheduler arguments on the sync path")
{
    auto io = fnfelt::monad::io::create([] { return 5; });
    CHECK_EQ(io().sync_wait(MockPool{}), 5);

    // Also constant-evaluable with a scheduler argument.
    static_assert(fnfelt::monad::io::create([] { return 6; })().sync_wait(MockPool{}) == 6);
}

TEST_CASE("RunProxy moves rvalue actions and copies lvalue actions")
{
    auto io = IO{CopyTrackingAction{}};
    static_assert(std::is_same_v<decltype(io)::action, CopyTrackingAction>);

    // Rvalue IO: the action is moved into the proxy.
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(std::move(io)().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 1);
    CHECK_EQ(CopyTrackingAction::copies, 0);

    // Mutable lvalue IO: the action is copied into the proxy.
    auto lvalue_io = IO{CopyTrackingAction{}};
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(lvalue_io().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 0);
    CHECK_EQ(CopyTrackingAction::copies, 1);

    // Const-lvalue IO: the action is copied into the proxy.
    IO const const_io{CopyTrackingAction{}};
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(const_io().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 0);
    CHECK_EQ(CopyTrackingAction::copies, 1);
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

    static_assert(IOTraits<>::validate_action(^^AsyncIntAction).empty());
    static_assert(
        IOTraits<>::validate_action(^^MissingValueAction) ==
        "provided action's async proxy does not expose a value_type alias");
    static_assert(
        IOTraits<>::validate_action(^^VoidValueAction) ==
        "provided action's async proxy must not produce void");
}

TEST_CASE("and_then, transform and ap see through async proxies' values")
{
    auto continuation = [](int value) { return IO{[value] { return value + 1; }}; };
    auto transformer = [](int value) { return value + 1; };

    // The async source's unwrapped value (int) is what the continuation/transformer must accept.
    static_assert(IOTraits<>::validate_and_then(^^async_int_io, ^^decltype(continuation)).empty());
    static_assert(IOTraits<>::validate_transform(^^async_int_io, ^^decltype(transformer)).empty());
    // The async function IO's callable accepts the async value IO's value.
    static_assert(IOTraits<>::validate_ap(^^async_fn_io, ^^async_int_io).empty());
}

TEST_CASE("sync pipelines remain constant-evaluable through sync_wait")
{
    static_assert(constexpr_multi_hop() == 7);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
