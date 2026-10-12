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
#include <fnfelt/monad/io/create.hpp>

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

/// Stateless async function doubling its argument.
inline constexpr auto async_double = [](auto, int x) -> lf::task<int> { co_return x * 2; };

/// Stateless async function producing a pair, for spread dispatch.
inline constexpr auto async_pair = [](auto, int x) -> lf::task<std::pair<int, int>>
{ co_return std::pair{x, x + 1}; };

/// Stateless async function consuming and producing a move-only value.
inline constexpr auto async_unique_from =
    [](auto, std::unique_ptr<int> ptr) -> lf::task<std::unique_ptr<int>>
{ co_return std::make_unique<int>(*ptr * 3); };

/// Stateless async function that throws, for exception propagation.
inline constexpr auto async_throwing = [](auto, int) -> lf::task<int>
{
    throw std::runtime_error{"async failure"};
    co_return 0;
};

/// Stateless async function taking a copy-tracking argument.
inline constexpr auto async_track = [](auto, CopyTrackingArg arg) -> lf::task<int>
{ co_return arg.value; };

/// Stateless coroutine functor with a deleted default constructor, for async-function validation.
struct DeletedDefaultCtorFn
{
    DeletedDefaultCtorFn() = delete;
    auto operator()(auto, int x) const -> lf::task<int>;
};

inline constexpr char async_io_name[] = "AsyncIO";
using AsyncIOTraits = fnfelt::monad::io::detail::CreateAsyncIOTraits<async_io_name>;

/// Custom traits delegating to CreateAsyncIOTraits, for the create_async injection point.
struct DelegatingCreateAsyncTraits : fnfelt::monad::io::detail::CreateIOTraits<>
{
    static constexpr std::string_view name = "DelegatingCreateAsyncIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::detail::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_create_async(
        std::meta::info fn_meta, std::meta::info arg_tuple_meta)
    {
        return fnfelt::monad::io::detail::CreateAsyncIOTraits<>::validate_create_async(
            fn_meta, arg_tuple_meta);
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

TEST_CASE("IORunProxy retains the IO traits and consults their sync_wait validator")
{
    auto io = fnfelt::monad::io::create<LvalueRunTraits>([] { return 42; });
    auto proxy = io.build();
    static_assert(std::is_same_v<
                  decltype(proxy),
                  fnfelt::monad::io::IORunProxy<decltype(io)::action_type, LvalueRunTraits>>);
    CHECK_EQ(proxy.sync_wait(), 42);
}

TEST_CASE("IORunProxy runs sync actions from rvalue, lvalue and const-lvalue IOs")
{
    auto io = fnfelt::monad::io::create([] { return 1; });
    auto const const_io = fnfelt::monad::io::create([] { return 2; });

    CHECK_EQ(fnfelt::monad::io::create([] { return 0; }).build().sync_wait(), 0);
    CHECK_EQ(io.build().sync_wait(), 1);
    CHECK_EQ(const_io.build().sync_wait(), 2);
}

TEST_CASE("IORunProxy runs a mutable action, which is only callable as an rvalue")
{
    // A mutable lambda has no const lvalue call operator; the run proxy copies it out of the lvalue
    // IO and invokes it as an rvalue, so the non-const call operator is selected.
    auto io = fnfelt::monad::io::create([value = 1] mutable { return value; });
    CHECK_EQ(io.build().sync_wait(), 1);
}

TEST_CASE("IORunProxy accepts and ignores scheduler arguments on the sync path")
{
    auto io = fnfelt::monad::io::create([] { return 5; });
    CHECK_EQ(io.build().sync_wait(MockPool{}), 5);

    // Also constant-evaluable with a scheduler argument.
    static_assert(fnfelt::monad::io::create([] { return 6; }).build().sync_wait(MockPool{}) == 6);
}

TEST_CASE("IORunProxy moves rvalue actions and copies lvalue actions")
{
    auto io = fnfelt::monad::io::create(CopyTrackingAction{});
    static_assert(std::is_same_v<decltype(io)::action_type, CopyTrackingAction>);

    // Rvalue IO: the action is moved into the proxy.
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(std::move(io).build().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 1);
    CHECK_EQ(CopyTrackingAction::copies, 0);

    // Mutable lvalue IO: the action is copied into the proxy.
    auto lvalue_io = fnfelt::monad::io::create(CopyTrackingAction{});
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(lvalue_io.build().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 0);
    CHECK_EQ(CopyTrackingAction::copies, 1);

    // Const-lvalue IO: the action is copied into the proxy.
    auto const const_io = fnfelt::monad::io::create(CopyTrackingAction{});
    CopyTrackingAction::moves = 0;
    CopyTrackingAction::copies = 0;
    CHECK_EQ(const_io.build().sync_wait(), 1);
    CHECK_EQ(CopyTrackingAction::moves, 0);
    CHECK_EQ(CopyTrackingAction::copies, 1);
}

TEST_CASE("create_async wraps a stateless coroutine function into a runnable IO")
{
    auto io = fnfelt::monad::io::create_async(async_double, 21);
    static_assert(std::is_same_v<decltype(io)::value_type, int>);

    CHECK_EQ(io.build().sync_wait(), 42);
    // An explicit scheduler is forwarded to the whole async tree.
    CHECK_EQ(io.build().sync_wait(lf::lazy_pool{2}), 42);
}

TEST_CASE("create_async can name the IO and take explicit traits")
{
    using namespace fnfelt::literals;  // NOLINT

    auto named = fnfelt::monad::io::create_async<"Async named"_ss>(async_double, 21);
    static_assert(decltype(named)::traits::name == "Async named");
    CHECK_EQ(named.build().sync_wait(), 42);

    auto custom = fnfelt::monad::io::create_async<AsyncIOTraits>(async_double, 21);
    static_assert(std::is_same_v<decltype(custom)::traits, AsyncIOTraits>);
    CHECK_EQ(custom.build().sync_wait(), 42);
}

TEST_CASE("create_async consults custom traits that delegate to the default")
{
    auto io = fnfelt::monad::io::create_async<DelegatingCreateAsyncTraits>(async_double, 21);
    static_assert(std::is_same_v<decltype(io)::traits, DelegatingCreateAsyncTraits>);
    static_assert(std::is_same_v<decltype(io)::value_type, int>);
    CHECK_EQ(io.build().sync_wait(), 42);
}

TEST_CASE("an exception thrown by an async leaf propagates to the caller")
{
    auto io = fnfelt::monad::io::create_async(async_throwing, 1);
    CHECK_THROWS_AS(static_cast<void>(io.build().sync_wait()), std::runtime_error);
}

TEST_CASE("CreateAsyncIOTraits::validate_create_async reports the first blocking constraint")
{
    // A plain function pointer is stateless and default-constructible, but not a class type, so it
    // cannot be stored as the proxy's `static constexpr TFn`.
    static_assert(
        fnfelt::monad::io::detail::CreateAsyncIOTraits<>::validate_create_async(
            ^^int (*)(int), ^^std::tuple<int>) == "provided async function must be a class type");

    // A stateless functor with a deleted default constructor fails the `static constexpr TFn`
    // requirement.
    static_assert(
        fnfelt::monad::io::detail::CreateAsyncIOTraits<>::validate_create_async(
            ^^DeletedDefaultCtorFn, ^^std::tuple<int>) ==
        "provided async function must be default-constructible");

    // A capturing closure is a class with state, so it is rejected as stateless before the
    // default-constructibility check.
    int const state = 1;
    auto const capturing = [state](auto, int x) -> lf::task<int> { co_return x + state; };
    static_assert(
        fnfelt::monad::io::detail::CreateAsyncIOTraits<>::validate_create_async(
            ^^decltype(capturing), ^^std::tuple<int>) ==
        "provided async function must be stateless");
}

TEST_CASE("running an async leaf twice from an lvalue copies its state per run")
{
    auto io = fnfelt::monad::io::create_async(async_track, CopyTrackingArg{});

    CopyTrackingArg::copies = 0;
    CopyTrackingArg::moves = 0;
    CHECK_EQ(io.build().sync_wait(), 1);
    int const copies_after_one_run = CopyTrackingArg::copies;
    CHECK_EQ(io.build().sync_wait(), 1);

    // Each `io.build()` on an lvalue copies the action (and its stored argument) into the
    // IORunProxy; from there the async machinery moves the argument, so each run adds exactly one
    // copy and the caller's IO is left untouched. Two runs therefore cost two copies.
    CHECK_EQ(copies_after_one_run, 1);
    CHECK_EQ(CopyTrackingArg::copies, 2);
}

TEST_CASE("create_async accepts a captureless mutable coroutine lambda")
{
    // libfork copies the coroutine function and invokes the copy as an rvalue, so a mutable
    // (non-const) call operator is accepted even though the proxy stores `fn` as a const static
    // member.
    auto io = fnfelt::monad::io::create_async(
        [](auto, int value) mutable -> lf::task<int> { co_return value; }, 42);
    CHECK_EQ(io.build().sync_wait(), 42);
}

TEST_CASE("IORunProxy executes a custom async proxy")
{
    CHECK_EQ(IO<AsyncIntAction>{AsyncIntAction{}}.build().sync_wait(), 0);
    CHECK_EQ(IO<AsyncIntAction>{AsyncIntAction{}}.build().sync_wait(lf::lazy_pool{2}), 0);
}

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
