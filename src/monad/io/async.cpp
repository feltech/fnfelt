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

#include <fnfelt/monad/io.hpp>

// Magic numbers are used in tests.
// NOLINTBEGIN(*-magic-numbers)
namespace
{
using fnfelt::monad::io::AndThenIOTraits;
using fnfelt::monad::io::ApIOTraits;
using fnfelt::monad::io::CreateAsyncIOTraits;
using fnfelt::monad::io::CreateIOTraits;
using fnfelt::monad::io::IO;
using fnfelt::monad::io::TransformIOTraits;

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

/// Constant-evaluated multi-hop pipeline: (1 + 1) * 2 + 3 == 7.
consteval int constexpr_multi_hop()
{
    using fnfelt::monad::io::detail::action_is_async;

    constexpr auto pipeline =
        fnfelt::monad::io::create([] { return 1; })
            .transform([](int x) { return x + 1; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 2; }); })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 3; }); });
    // A purely synchronous chain must stay on the sync arm.
    static_assert(!action_is_async(^^decltype(pipeline)::action_type));
    return pipeline.build().sync_wait();
}

/// Constant-evaluated sync chain that coexists with the async runtime tests in this TU.
consteval int constexpr_sync_chain_with_async()
{
    constexpr auto pipeline =
        fnfelt::monad::io::create([] { return 2; })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 3; }); });
    return pipeline.build().sync_wait();
}

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
using AsyncIOTraits = CreateAsyncIOTraits<async_io_name>;

/// Custom traits delegating to the default traits, for the async and_then injection point.
struct DelegatingAndThenTraits
{
    static constexpr std::string_view name = "DelegatingAndThenIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_and_then(
        std::meta::info io_meta, std::meta::info continuation_meta)
    {
        return fnfelt::monad::io::AndThenIOTraits<>::validate_and_then(io_meta, continuation_meta);
    }
};

/// Custom traits delegating to CreateAsyncIOTraits, for the create_async injection point.
struct DelegatingCreateAsyncTraits
{
    static constexpr std::string_view name = "DelegatingCreateAsyncIO";

    static consteval std::string_view validate_action(std::meta::info action_meta)
    {
        return fnfelt::monad::io::CreateIOTraits<>::validate_action(action_meta);
    }

    static consteval std::string_view validate_create_async(
        std::meta::info fn_meta, std::meta::info arg_tuple_meta)
    {
        return fnfelt::monad::io::CreateAsyncIOTraits<>::validate_create_async(
            fn_meta, arg_tuple_meta);
    }
};
}  // namespace

TEST_CASE("RunProxy runs sync actions from rvalue, lvalue and const-lvalue IOs")
{
    auto io = fnfelt::monad::io::create([] { return 1; });
    auto const const_io = fnfelt::monad::io::create([] { return 2; });

    CHECK_EQ(fnfelt::monad::io::create([] { return 0; }).build().sync_wait(), 0);
    CHECK_EQ(io.build().sync_wait(), 1);
    CHECK_EQ(const_io.build().sync_wait(), 2);
}

TEST_CASE("RunProxy runs a mutable action, which is only callable as an rvalue")
{
    // A mutable lambda has no const lvalue call operator; the run proxy copies it out of the lvalue
    // IO and invokes it as an rvalue, so the non-const call operator is selected.
    auto io = fnfelt::monad::io::create([value = 1] mutable { return value; });
    CHECK_EQ(io.build().sync_wait(), 1);
}

TEST_CASE("RunProxy accepts and ignores scheduler arguments on the sync path")
{
    auto io = fnfelt::monad::io::create([] { return 5; });
    CHECK_EQ(io.build().sync_wait(MockPool{}), 5);

    // Also constant-evaluable with a scheduler argument.
    static_assert(fnfelt::monad::io::create([] { return 6; }).build().sync_wait(MockPool{}) == 6);
}

TEST_CASE("RunProxy moves rvalue actions and copies lvalue actions")
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

    static_assert(CreateIOTraits<>::validate_action(^^AsyncIntAction).empty());
    static_assert(
        CreateIOTraits<>::validate_action(^^MissingValueAction) ==
        "provided action's async proxy does not expose a value_type alias");
    static_assert(
        CreateIOTraits<>::validate_action(^^VoidValueAction) ==
        "provided action's async proxy must not produce void");
}

TEST_CASE("and_then, transform and ap see through async proxies' values")
{
    auto continuation = [](int value)
    { return fnfelt::monad::io::create([value] { return value + 1; }); };
    auto transformer = [](int value) { return value + 1; };

    // The async source's unwrapped value (int) is what the continuation/transformer must accept.
    static_assert(
        AndThenIOTraits<>::validate_and_then(^^async_int_io, ^^decltype(continuation)).empty());
    static_assert(
        TransformIOTraits<>::validate_transform(^^async_int_io, ^^decltype(transformer)).empty());
    // The async function IO's callable accepts the async value IO's value.
    static_assert(ApIOTraits<>::validate_ap(^^async_fn_io, ^^async_int_io).empty());
}

TEST_CASE("AndThenIOTraits::validate_and_then sees through an async source's unwrapped value")
{
    using async_source_io = decltype(fnfelt::monad::io::create_async(async_double, 21));

    // The validator unwraps the async proxy, so a continuation returning a non-IO is rejected for
    // the same reason as on the synchronous path.
    static_assert(
        AndThenIOTraits<>::validate_and_then(
            ^^async_source_io, ^^decltype([](int) { return 1; })) ==
        "continuation function must return an IO");
    // The unwrapped value (int) is what the continuation must accept: one taking a string does not.
    static_assert(
        AndThenIOTraits<>::validate_and_then(
            ^^async_source_io, ^^decltype([](std::string) { return 1; })) ==
        "continuation function does not accept the source IO's value");
}

TEST_CASE("sync pipelines remain constant-evaluable through sync_wait")
{
    static_assert(constexpr_multi_hop() == 7);
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

TEST_CASE("and_then runs sync continuations over async sources and vice versa")
{
    using fnfelt::monad::io::detail::action_is_async;

    // Sync over async through the free function: 10 -> async(*2)=20 -> sync(+1)=21.
    auto sync_over_async = fnfelt::monad::io::and_then(
        fnfelt::monad::io::create_async(async_double, 10),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    // A mixed composition containing an async source must take the async arm.
    static_assert(action_is_async(^^decltype(sync_over_async)::action_type));
    CHECK_EQ(sync_over_async.build().sync_wait(), 21);

    // Async over sync through the IO member: 5 -> sync -> async(*10)=50.
    auto async_over_sync =
        fnfelt::monad::io::create([] { return 5; })
            .and_then(
                [](int x)
                {
                    return fnfelt::monad::io::create_async(
                        [](auto, int y) -> lf::task<int> { co_return y * 10; }, x);
                });
    // A mixed composition containing an async continuation must take the async arm.
    static_assert(action_is_async(^^decltype(async_over_sync)::action_type));
    CHECK_EQ(async_over_sync.build().sync_wait(), 50);
}

TEST_CASE("and_then consults custom traits that delegate to the default on the async path")
{
    using fnfelt::monad::io::detail::action_is_async;

    auto bound = fnfelt::monad::io::and_then<DelegatingAndThenTraits>(
        fnfelt::monad::io::create_async(async_double, 10),
        [](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); });
    static_assert(std::is_same_v<decltype(bound)::traits, DelegatingAndThenTraits>);
    static_assert(action_is_async(^^decltype(bound)::action_type));
    CHECK_EQ(bound.build().sync_wait(lf::lazy_pool{2}), 21);
}

TEST_CASE("and_then composes a deep alternating sync and async chain")
{
    using fnfelt::monad::io::detail::action_is_async;

    // 1 -> async(*2)=2 -> sync(+1)=3 -> async(*2)=6 -> sync(*2)=12, under one scheduler.
    auto chain =
        fnfelt::monad::io::create([] { return 1; })
            .and_then([](int x) { return fnfelt::monad::io::create_async(async_double, x); })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x + 1; }); })
            .and_then([](int x) { return fnfelt::monad::io::create_async(async_double, x); })
            .and_then([](int x) { return fnfelt::monad::io::create([x] { return x * 2; }); });
    // The alternating chain contains async hops throughout, so the whole composition stays async.
    static_assert(action_is_async(^^decltype(chain)::action_type));
    CHECK_EQ(chain.build().sync_wait(lf::lazy_pool{2}), 12);
}

TEST_CASE("and_then carries an async move-only value into a continuation")
{
    auto bound = fnfelt::monad::io::create_async(async_unique_from, std::make_unique<int>(4))
                     .and_then([](std::unique_ptr<int> ptr)
                               { return fnfelt::monad::io::create([v = *ptr] { return v + 1; }); });
    static_assert(std::is_same_v<decltype(bound)::value_type, int>);
    // The leaf stores a move-only argument, so the composed action and its IO are move-only.
    static_assert(!std::is_copy_constructible_v<decltype(bound)>);
    CHECK_EQ(std::move(bound).build().sync_wait(), 13);
}

TEST_CASE("and_then spreads an async value into a multi-argument continuation")
{
    // The pair produced by the async leaf is spread through the dealiased async value type.
    auto spread =
        fnfelt::monad::io::create_async(async_pair, 3)
            .and_then([](int lhs, int rhs)
                      { return fnfelt::monad::io::create([lhs, rhs] { return lhs * 10 + rhs; }); });
    CHECK_EQ(spread.build().sync_wait(), 34);
}

TEST_CASE("and_then invokes an async continuation as an rvalue")
{
    // Ref-qualified continuation: the `&&` overload is the one validation, inference and the
    // driver must all select, so the composition's value is long (2) rather than int (1).
    struct RefQualifiedContinuation
    {
        auto operator()([[maybe_unused]] int value) const &
        {
            return fnfelt::monad::io::create([] { return 1; });
        }

        auto operator()([[maybe_unused]] int value) &&
        {
            return fnfelt::monad::io::create([] { return 2L; });
        }
    };

    auto bound =
        fnfelt::monad::io::create_async(async_double, 10).and_then(RefQualifiedContinuation{});
    // NOLINTNEXTLINE(runtime/int): the `&&` overload returns the fundamental `long`.
    static_assert(std::is_same_v<decltype(bound)::value_type, long>);
    CHECK_EQ(bound.build().sync_wait(lf::lazy_pool{2}), 2L);
}

TEST_CASE("an exception thrown by an async leaf propagates to the caller")
{
    auto io = fnfelt::monad::io::create_async(async_throwing, 1);
    CHECK_THROWS_AS(static_cast<void>(io.build().sync_wait()), std::runtime_error);
}

TEST_CASE("async leaf and composed actions pass action validation")
{
    using leaf_io = decltype(fnfelt::monad::io::create_async(async_double, 21));
    using composed_io =
        decltype(fnfelt::monad::io::create_async(async_double, 21)
                     .and_then([](int x)
                               { return fnfelt::monad::io::create([x] { return x + 1; }); }));

    static_assert(CreateIOTraits<>::validate_action(^^leaf_io::action_type).empty());
    static_assert(CreateIOTraits<>::validate_action(^^composed_io::action_type).empty());
    static_assert(std::is_same_v<leaf_io::value_type, int>);
    static_assert(std::is_same_v<composed_io::value_type, int>);
}

TEST_CASE("CreateAsyncIOTraits::validate_create_async reports the first blocking constraint")
{
    // A plain function pointer is stateless and default-constructible, but not a class type, so it
    // cannot be stored as the proxy's `static constexpr TFn`.
    static_assert(
        CreateAsyncIOTraits<>::validate_create_async(^^int (*)(int), ^^std::tuple<int>) ==
        "provided async function must be a class type");

    // A stateless functor with a deleted default constructor fails the `static constexpr TFn`
    // requirement.
    static_assert(
        CreateAsyncIOTraits<>::validate_create_async(^^DeletedDefaultCtorFn, ^^std::tuple<int>) ==
        "provided async function must be default-constructible");

    // A capturing closure is a class with state, so it is rejected as stateless before the
    // default-constructibility check.
    int const state = 1;
    auto const capturing = [state](auto, int x) -> lf::task<int> { co_return x + state; };
    static_assert(
        CreateAsyncIOTraits<>::validate_create_async(^^decltype(capturing), ^^std::tuple<int>) ==
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

    // Each `io.build()` on an lvalue copies the action (and its stored argument) into the RunProxy;
    // from there the async machinery moves the argument, so each run adds exactly one copy and the
    // caller's IO is left untouched. Two runs therefore cost two copies.
    CHECK_EQ(copies_after_one_run, 1);
    CHECK_EQ(CopyTrackingArg::copies, 2);
}

TEST_CASE("constexpr sync chains coexist with async runtime tests")
{
    static_assert(constexpr_sync_chain_with_async() == 6);
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

// NOLINTEND(*-magic-numbers)
#endif  // DOCTEST_CONFIG_DISABLE
