// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/interop/stdexec.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <libfork/core.hpp>
#include <libfork/schedule.hpp>
#include <stdexec/execution.hpp>

#include <fnfelt/monad/io.hpp>
#include <fnfelt/monad/reader_io/async.hpp>

namespace
{
using fnfelt::interop::await_sender;
using fnfelt::interop::sender_outcome_t;

constexpr auto await_action = []<class TSender>(
                                  auto self, TSender sender) -> lf::task<sender_outcome_t<TSender>>
{ co_return co_await await_sender(self.context(), std::move(sender)); };

template <class TSender>
auto run_sender(TSender sender)
{
    return fnfelt::monad::io::create_async(await_action, std::move(sender))
        .build()
        .sync_wait(lf::lazy_pool{2});
}

/// Operation lifetime counters for detecting missing or repeated cleanup.
struct OperationCounts
{
    int constructed = 0;
    int destroyed = 0;
};

/**
 * Sender fixture with chosen completion signatures and programmable completion behavior.
 *
 * Supports inline and external completion, connection failure, and operation lifetime checks.
 *
 * @tparam TSignatures Advertised completion signatures.
 * @tparam TStart Completion callback type.
 */
template <class TSignatures, class TStart>
struct TestSender
{
    using sender_concept = stdexec::sender_tag;
    using completion_signatures = TSignatures;

    TStart start_fn;
    OperationCounts * counts = nullptr;
    bool should_throw_connect = false;

    /**
     * Immovable operation fixture for verifying direct construction and receiver lifetime.
     *
     * @tparam TReceiver Receiver retaining the completion destination.
     */
    template <class TReceiver>
    struct Operation
    {
        using operation_state_concept = stdexec::operation_state_tag;

        TReceiver receiver;
        TStart start_fn;
        OperationCounts * counts;

        Operation(TReceiver receiver, TStart start_fn, OperationCounts * counts)
            : receiver(std::move(receiver)), start_fn(std::move(start_fn)), counts(counts)
        {
            if (counts != nullptr)
            {
                ++counts->constructed;
            }
        }

        Operation(Operation const &) = delete;
        Operation(Operation &&) = delete;
        Operation & operator=(Operation const &) = delete;
        Operation & operator=(Operation &&) = delete;

        ~Operation()
        {
            if (counts != nullptr)
            {
                ++counts->destroyed;
            }
        }

        void start() noexcept
        {
            // Completion may destroy the operation, so invoke through independent locals.
            auto callback = std::move(start_fn);
            auto target = std::move(receiver);
            std::move(callback)(std::move(target));
        }
    };

    template <class TReceiver>
    auto connect(TReceiver receiver) &&
    {
        if (should_throw_connect)
        {
            throw std::runtime_error("connect failed");
        }
        return Operation<TReceiver>{std::move(receiver), std::move(start_fn), counts};
    }
};

template <class TSignatures, class TStart>
auto test_sender(TStart start_fn, OperationCounts * counts = nullptr, bool should_throw = false)
{
    return TestSender<TSignatures, TStart>{std::move(start_fn), counts, should_throw};
}

/**
 * Counted payload with selectable copy and move failures.
 *
 * Exercises exception handling and cleanup while storing and retrieving completion values.
 */
struct ThrowingPayload
{
    int * live;
    bool should_throw_copy;
    bool should_throw_move;

    ThrowingPayload(int * live, bool const should_throw_copy, bool const should_throw_move)
        : live(live), should_throw_copy(should_throw_copy), should_throw_move(should_throw_move)
    {
        ++*live;
    }

    ThrowingPayload(ThrowingPayload const & other)
        : live(other.live),
          should_throw_copy(other.should_throw_copy),
          should_throw_move(other.should_throw_move)
    {
        if (should_throw_copy)
        {
            throw std::runtime_error("copy failed");
        }
        ++*live;
    }

    ThrowingPayload(ThrowingPayload && other) noexcept(false)
        : live(other.live),
          should_throw_copy(other.should_throw_copy),
          should_throw_move(other.should_throw_move)
    {
        if (should_throw_move)
        {
            throw std::runtime_error("move failed");
        }
        ++*live;
    }

    ThrowingPayload & operator=(ThrowingPayload const &) = default;
    ThrowingPayload & operator=(ThrowingPayload &&) = default;

    ~ThrowingPayload()
    {
        --*live;
    }
};
/// Receiver environment fixture exposing a stop token and a successful result value.
struct StopEnvironment
{
    stdexec::inplace_stop_token token;
    int number;

    // stdexec queries this customization through its execution-property dispatch.
    [[maybe_unused]] auto query(stdexec::get_stop_token_t) const noexcept
    {
        return token;
    }
};

/**
 * Sender fixture whose signatures and completion depend on its receiver environment.
 *
 * Exercises exact environment-type matching and stop-token propagation.
 */
struct EnvironmentSender
{
    // stdexec inspects this protocol marker rather than referencing it directly.
    using sender_concept [[maybe_unused]] = stdexec::sender_tag;

    template <class, class TEnvironment>
    static consteval auto get_completion_signatures()
    {
        if constexpr (std::is_same_v<TEnvironment, StopEnvironment const &>)
        {
            return stdexec::completion_signatures<
                stdexec::set_value_t(int const &),
                stdexec::set_stopped_t()>{};
        }
        else
        {
            return stdexec::completion_signatures<stdexec::set_value_t(std::string)>{};
        }
    }

    template <class TReceiver>
    auto connect(TReceiver receiver) &&
    {
        using environment_type = decltype(stdexec::get_env(receiver));
        using signatures =
            decltype(get_completion_signatures<EnvironmentSender &&, environment_type>());
        return test_sender<signatures>(
                   [](auto target)
                   {
                       auto const & environment = stdexec::get_env(target);
                       if constexpr (std::is_same_v<environment_type, StopEnvironment const &>)
                       {
                           if (stdexec::get_stop_token(environment).stop_requested())
                           {
                               stdexec::set_stopped(std::move(target));
                           }
                           else
                           {
                               stdexec::set_value(std::move(target), environment.number);
                           }
                       }
                       else
                       {
                           stdexec::set_value(std::move(target), std::string("default"));
                       }
                   })
            .connect(std::move(receiver));
    }
};

/// Copy and move counters observed before any result leaves the awaiting coroutine.
struct PayloadCounts
{
    int copies = 0;
    int moves = 0;
};

/// Sender payload isolating storage-to-Result adoption from later task transports.
struct CountedPayload
{
    PayloadCounts * counts;
    int number;

    CountedPayload(PayloadCounts * observed, int value) : counts{observed}, number{value} {}
    CountedPayload(CountedPayload const & other) : counts{other.counts}, number{other.number}
    {
        ++counts->copies;
    }
    CountedPayload(CountedPayload && other) noexcept : counts{other.counts}, number{other.number}
    {
        ++counts->moves;
    }
    CountedPayload & operator=(CountedPayload const &) = default;
    CountedPayload & operator=(CountedPayload &&) = default;
};
}  // namespace

TEST_CASE("stdexec bridge normalizes inline success completions")
{
    auto empty = run_sender(stdexec::just());
    using empty_outcome = fnfelt::Result<std::monostate, fnfelt::Infallible>;
    static_assert(std::is_same_v<decltype(empty), empty_outcome>);
    REQUIRE(empty.has_value());
    REQUIRE(empty->has_value());

    auto single = run_sender(stdexec::just(42));
    static_assert(std::is_same_v<decltype(single), fnfelt::Result<int, fnfelt::Infallible>>);
    REQUIRE(single.has_value());
    REQUIRE(single->has_value());
    CHECK_EQ(**single, 42);

    auto multiple = run_sender(stdexec::just(std::string("data"), 4));
    REQUIRE(multiple.has_value());
    REQUIRE(multiple->has_value());
    CHECK_EQ(std::get<0>(**multiple), "data");
    CHECK_EQ(std::get<1>(**multiple), 4);
}

TEST_CASE("stdexec bridge preserves typed errors and stopped completion")
{
    auto const error = std::make_error_code(std::errc::io_error);
    auto failed = run_sender(stdexec::just_error(error));
    static_assert(
        std::is_same_v<decltype(failed), fnfelt::Result<std::monostate, std::error_code>>);
    REQUIRE((failed && !failed->has_value()));
    CHECK_EQ(failed.value().error().value, error);

    auto flag = run_sender(stdexec::just_error(std::monostate{}));
    static_assert(std::is_same_v<decltype(flag), fnfelt::Result<std::monostate, std::monostate>>);
    CHECK(fnfelt::result::is_error(flag));

    auto stopped = run_sender(stdexec::just_stopped());
    static_assert(
        std::is_same_v<decltype(stopped), fnfelt::Result<std::monostate, fnfelt::Infallible>>);
    CHECK(stopped == std::nullopt);
}

TEST_CASE("stdexec bridge moves owned results")
{
    auto result = run_sender(stdexec::just(std::make_unique<int>(42)));
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK_EQ(***result, 42);
}

TEST_CASE("stdexec bridge permits expected construction tags as sender error data")
{
    auto in_place = run_sender(stdexec::just_error(std::in_place));
    static_assert(
        std::is_same_v<decltype(in_place), fnfelt::Result<std::monostate, std::in_place_t>>);
    CHECK(fnfelt::result::is_error(in_place));

    auto unexpect = run_sender(stdexec::just_error(std::unexpect));
    static_assert(
        std::is_same_v<decltype(unexpect), fnfelt::Result<std::monostate, std::unexpect_t>>);
    CHECK(fnfelt::result::is_error(unexpect));

    auto unexpected = run_sender(stdexec::just_error(std::unexpected{42}));
    static_assert(
        std::is_same_v<decltype(unexpected), fnfelt::Result<std::monostate, std::unexpected<int>>>);
    REQUIRE(fnfelt::result::is_error(unexpected));
    CHECK_EQ(fnfelt::result::get_error(unexpected).error(), 42);
}

TEST_CASE("stdexec bridge adopts completion storage with one payload move")
{
    using signatures = stdexec::completion_signatures<stdexec::set_value_t(CountedPayload const &)>;
    PayloadCounts counts;
    CountedPayload const payload{&counts, 42};
    auto sender = test_sender<signatures>([&payload](auto receiver)
                                          { stdexec::set_value(std::move(receiver), payload); });
    constexpr auto inspect = []<class TSender>(
                                 auto self, TSender source) -> lf::task<std::tuple<int, int, int>>
    {
        auto result = co_await await_sender(self.context(), std::move(source));
        // Snapshot here: returning the Result through libfork or IO would add unrelated moves.
        co_return std::tuple{
            result.value().value().number,
            result.value().value().counts->copies,
            result.value().value().counts->moves};
    };
    auto const [number, copies, moves] =
        fnfelt::monad::io::create_async(inspect, std::move(sender)).build().sync_wait();
    CHECK_EQ(number, 42);
    CHECK_EQ(copies, 1);
    CHECK_EQ(moves, 1);
}

TEST_CASE("stdexec bridge preserves empty optional values and errors as data")
{
    auto success = run_sender(stdexec::just(std::optional<int>{}));
    REQUIRE(success.has_value());
    REQUIRE(success->has_value());
    CHECK_FALSE(success.value().value().has_value());
    CHECK_FALSE(success == std::nullopt);

    auto failure = run_sender(stdexec::just_error(std::optional<int>{}));
    REQUIRE((failure && !failure->has_value()));
    CHECK_FALSE(failure.value().error().value.has_value());
    CHECK_FALSE(failure == std::nullopt);
}

TEST_CASE("stdexec bridge promotes plain IO continuations without losing terminal outcomes")
{
    using signatures = stdexec::completion_signatures<
        stdexec::set_value_t(int),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>;
    auto const error = std::make_error_code(std::errc::io_error);
    for (int mode = 0; mode != 3; ++mode)
    {
        CAPTURE(mode);
        int generic_invocations = 0;
        int plain_io_invocations = 0;
        int plain_value_invocations = 0;
        auto source = test_sender<signatures>(
            [mode, error](auto receiver)
            {
                if (mode == 0)
                    stdexec::set_value(std::move(receiver), 40);
                else if (mode == 1)
                    stdexec::set_error(std::move(receiver), std::error_code{error});
                else
                    stdexec::set_stopped(std::move(receiver));
            });
        using expected_type = sender_outcome_t<decltype(source)>;
        auto program = fnfelt::monad::io::create_async(await_action, std::move(source))
                           .and_then(
                               [&](auto value)
                               {
                                   static_assert(std::is_same_v<decltype(value), int>);
                                   ++generic_invocations;
                                   return fnfelt::monad::io::just(value + 1);
                               })
                           .and_then(
                               [&](int value)
                               {
                                   ++plain_io_invocations;
                                   return fnfelt::monad::io::just(value + 1);
                               })
                           .transform(
                               [&](int value)
                               {
                                   ++plain_value_invocations;
                                   return value;
                               });
        static_assert(std::is_same_v<typename decltype(program)::value_type, expected_type>);
        auto result = program.build().sync_wait(lf::lazy_pool{2});
        CHECK_EQ(generic_invocations, mode == 0 ? 1 : 0);
        CHECK_EQ(plain_io_invocations, mode == 0 ? 1 : 0);
        CHECK_EQ(plain_value_invocations, mode == 0 ? 1 : 0);
        if (mode == 0)
        {
            REQUIRE(result.has_value());
            REQUIRE(result->has_value());
            CHECK_EQ(result.value().value(), 42);
        }
        else if (mode == 1)
        {
            REQUIRE((result && !result->has_value()));
            CHECK_EQ(result.value().error().value, error);
        }
        else
            CHECK(result == std::nullopt);
    }
}

TEST_CASE("stdexec bridge resumes on its worker after foreign completion during start")
{
    using signatures = stdexec::completion_signatures<stdexec::set_value_t(int)>;
    std::jthread completion_thread;
    std::latch delivered{1};
    std::thread::id callback_thread;
    OperationCounts counts;
    auto sender = test_sender<signatures>(
        [&](auto receiver)
        {
            completion_thread = std::jthread(
                [receiver = std::move(receiver), &delivered, &callback_thread]() mutable
                {
                    callback_thread = std::this_thread::get_id();
                    stdexec::set_value(std::move(receiver), 42);
                    delivered.count_down();
                });
            // Force completion before start returns, without a timing-dependent sleep.
            delivered.wait();
        },
        &counts);
    constexpr auto action = []<class TSender>(auto self, TSender source_sender)
        -> lf::task<std::tuple<sender_outcome_t<TSender>, bool, std::thread::id>>
    {
        auto * current = self.context();
        auto result = co_await await_sender(current, std::move(source_sender));
        co_return std::tuple{
            std::move(result), self.context() == current, std::this_thread::get_id()};
    };
    auto [result, is_same_worker, resumed_thread] =
        fnfelt::monad::io::create_async(action, std::move(sender))
            .build()
            .sync_wait(lf::lazy_pool{2});
    completion_thread.join();

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK_EQ(**result, 42);
    CHECK(is_same_worker);
    CHECK_NE(resumed_thread, callback_thread);
    CHECK_EQ(counts.constructed, 1);
    CHECK_EQ(counts.destroyed, 1);
}

TEST_CASE("stdexec bridge cleans up a failed connection")
{
    using signatures = stdexec::completion_signatures<stdexec::set_value_t(int)>;
    OperationCounts counts;
    auto sender = test_sender<signatures>(
        [](auto receiver) { stdexec::set_value(std::move(receiver), 42); }, &counts, true);
    CHECK_THROWS_WITH_AS(run_sender(std::move(sender)), "connect failed", std::runtime_error);
    CHECK_EQ(counts.constructed, 0);
    CHECK_EQ(counts.destroyed, 0);
}

TEST_CASE("stdexec bridge permits worker progress while an external operation is pending")
{
    using signatures = stdexec::completion_signatures<stdexec::set_value_t(int)>;
    std::jthread completion_thread;
    std::latch release{1};
    OperationCounts counts;
    auto sender = test_sender<signatures>(
        [&](auto receiver)
        {
            completion_thread = std::jthread(
                [receiver = std::move(receiver), &release]() mutable
                {
                    release.wait();
                    stdexec::set_value(std::move(receiver), 42);
                });
        },
        &counts);
    constexpr auto parent = []<class TSender>(
                                auto,
                                TSender source_sender,
                                std::latch * completion_gate) -> lf::task<sender_outcome_t<TSender>>
    {
        lf::eventually<sender_outcome_t<TSender>> result;
        co_await lf::fork[&result, await_action](std::move(source_sender));
        // With one worker, this continuation can only run after the child suspends.
        completion_gate->count_down();
        co_await lf::join;
        co_return std::move(*result);
    };
    auto result = fnfelt::monad::io::create_async(parent, std::move(sender), &release)
                      .build()
                      .sync_wait(lf::lazy_pool{1});
    completion_thread.join();
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK_EQ(**result, 42);
    CHECK_EQ(counts.constructed, 1);
    CHECK_EQ(counts.destroyed, 1);
}

TEST_CASE("stdexec bridge accepts successful construction without noexcept declarations")
{
    using value_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(ThrowingPayload const &)>;
    using error_signatures =
        stdexec::completion_signatures<stdexec::set_error_t(ThrowingPayload const &)>;
    int live = 0;
    ThrowingPayload payload{&live, false, false};
    static_assert(!std::is_nothrow_copy_constructible_v<ThrowingPayload>);
    static_assert(!std::is_nothrow_move_constructible_v<ThrowingPayload>);

    SUBCASE("successful value copy and move")
    {
        OperationCounts counts;
        auto result = run_sender(
            test_sender<value_signatures>(
                [&payload](auto receiver) { stdexec::set_value(std::move(receiver), payload); },
                &counts));
        REQUIRE((result && result->has_value()));
        CHECK_EQ(result.value().value().live, &live);
        CHECK_EQ(live, 2);
        CHECK_EQ(counts.destroyed, 1);
    }

    SUBCASE("successful error copy and move")
    {
        auto result = run_sender(
            test_sender<error_signatures>([&payload](auto receiver)
                                          { stdexec::set_error(std::move(receiver), payload); }));
        REQUIRE((result && !result->has_value()));
        CHECK_EQ(result.value().error().value.live, &live);
        CHECK_EQ(live, 2);
    }
    SUBCASE("successful value move during completion")
    {
        using signatures = stdexec::completion_signatures<stdexec::set_value_t(ThrowingPayload)>;
        auto result = run_sender(
            test_sender<signatures>(
                [&payload](auto receiver)
                { stdexec::set_value(std::move(receiver), std::move(payload)); }));
        REQUIRE(fnfelt::result::is_success(result));
        CHECK_EQ(fnfelt::result::get_success(result).live, &live);
        CHECK_EQ(live, 2);
    }
    SUBCASE("successful error move during completion")
    {
        using signatures = stdexec::completion_signatures<stdexec::set_error_t(ThrowingPayload)>;
        auto result = run_sender(
            test_sender<signatures>(
                [&payload](auto receiver)
                { stdexec::set_error(std::move(receiver), std::move(payload)); }));
        REQUIRE(fnfelt::result::is_error(result));
        CHECK_EQ(fnfelt::result::get_error(result).live, &live);
        CHECK_EQ(live, 2);
    }
    CHECK_EQ(live, 1);
}

TEST_CASE("stdexec bridge releases its operation when moving the outcome throws")
{
    using signatures =
        stdexec::completion_signatures<stdexec::set_value_t(ThrowingPayload const &)>;
    int live = 0;
    ThrowingPayload const payload{&live, false, true};
    OperationCounts counts;
    auto sender = test_sender<signatures>(
        [&payload](auto receiver) { stdexec::set_value(std::move(receiver), payload); }, &counts);
    CHECK_THROWS_WITH_AS(run_sender(std::move(sender)), "move failed", std::runtime_error);
    CHECK_EQ(counts.constructed, 1);
    CHECK_EQ(counts.destroyed, 1);
    CHECK_EQ(live, 1);
}

TEST_CASE("stdexec bridge deduplicates normalized alternatives without flattening payloads")
{
    using pair_type = std::tuple<int, int>;
    using payload_type = std::variant<int, std::string>;
    using signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_value_t(std::monostate),
        stdexec::set_value_t(int),
        stdexec::set_value_t(int const &),
        stdexec::set_value_t(int, int),
        stdexec::set_value_t(pair_type),
        stdexec::set_value_t(payload_type),
        stdexec::set_error_t(std::error_code),
        stdexec::set_error_t(std::string),
        stdexec::set_error_t(std::error_code const &)>;
    auto const error = std::make_error_code(std::errc::io_error);

    for (int mode = 0; mode != 10; ++mode)
    {
        CAPTURE(mode);
        auto result = run_sender(
            test_sender<signatures>(
                [mode, error](auto receiver)
                {
                    switch (mode)
                    {
                        case 0:
                            stdexec::set_value(std::move(receiver));
                            break;
                        case 1:
                            stdexec::set_value(std::move(receiver), std::monostate{});
                            break;
                        case 2:
                            stdexec::set_value(std::move(receiver), 42);
                            break;
                        case 3:
                        {
                            constexpr int value = 42;
                            stdexec::set_value(std::move(receiver), value);
                            break;
                        }
                        case 4:
                            stdexec::set_value(std::move(receiver), 42, 43);
                            break;
                        case 5:
                            stdexec::set_value(std::move(receiver), pair_type{42, 43});
                            break;
                        case 6:
                            stdexec::set_value(std::move(receiver), payload_type{42});
                            break;
                        case 7:
                            stdexec::set_error(std::move(receiver), std::error_code(error));
                            break;
                        case 8:
                            stdexec::set_error(std::move(receiver), std::string("failed"));
                            break;
                        default:
                            stdexec::set_error(std::move(receiver), error);
                            break;
                    }
                }));
        using value_type = typename decltype(result)::value_type::value_type;
        using error_type = std::remove_cvref_t<decltype(result->error().value)>;
        static_assert(std::variant_size_v<value_type> == 4);
        static_assert(std::variant_size_v<error_type> == 2);
        CHECK_FALSE(result == std::nullopt);
        if (mode < 7)
        {
            REQUIRE(result.has_value());
            REQUIRE(result->has_value());
            if (mode < 2)
            {
                CHECK(std::holds_alternative<std::monostate>(**result));
            }
            else if (mode < 4)
            {
                CHECK_EQ(std::get<int>(**result), 42);
            }
            else if (mode < 6)
            {
                CHECK_EQ(std::get<pair_type>(**result), pair_type{42, 43});
            }
            else
            {
                CHECK_EQ(std::get<int>(std::get<payload_type>(**result)), 42);
            }
        }
        else
        {
            REQUIRE((result && !result->has_value()));
            if (mode == 8)
            {
                CHECK_EQ(std::get<std::string>(result.value().error().value), "failed");
            }
            else
            {
                CHECK_EQ(std::get<std::error_code>(result.value().error().value), error);
            }
        }
    }

    auto direct_variant = run_sender(stdexec::just(payload_type{42}));
    static_assert(
        std::is_same_v<typename decltype(direct_variant)::value_type::value_type, payload_type>);
    REQUIRE(direct_variant.has_value());
    REQUIRE(direct_variant->has_value());
    CHECK_EQ(std::get<int>(**direct_variant), 42);
}

TEST_CASE("stdexec bridge exposes an owned receiver environment supplied through ReaderIO")
{
    /// ReaderIO state carrying receiver properties through an asynchronous composition.
    struct Runtime
    {
        StopEnvironment receiver_environment;
    };
    constexpr auto action = [](auto self, Runtime const & runtime, EnvironmentSender sender)
        -> lf::task<sender_outcome_t<EnvironmentSender, StopEnvironment>>
    {
        co_return co_await await_sender(
            self.context(), std::move(sender), runtime.receiver_environment);
    };
    auto program = fnfelt::monad::reader_io::create_async<Runtime>(action, EnvironmentSender{});
    stdexec::inplace_stop_source stop_source;
    Runtime runtime{.receiver_environment = {.token = stop_source.get_token(), .number = 42}};
    auto result = program.build(runtime).sync_wait(lf::lazy_pool{2});
    static_assert(std::is_same_v<typename decltype(result)::value_type::value_type, int>);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK_EQ(**result, 42);

    stop_source.request_stop();
    CHECK(program.build(runtime).sync_wait(lf::lazy_pool{2}) == std::nullopt);

    auto default_result = run_sender(EnvironmentSender{});
    static_assert(
        std::is_same_v<typename decltype(default_result)::value_type::value_type, std::string>);
    REQUIRE(default_result.has_value());
    REQUIRE(default_result->has_value());
    CHECK_EQ(**default_result, "default");
}

TEST_CASE("stdexec bridge diagnoses unsupported owned and direct channel types")
{
    using fnfelt::interop::detail::validate_completion;
    using fnfelt::interop::detail::validate_expected_value;
    /// Incomplete type fixture for rejecting completion arguments without a complete stored type.
    struct Incomplete;
    /// Move-only type fixture for rejecting completion references that require copying.
    struct NoCopy
    {
        NoCopy(NoCopy const &) = delete;
        NoCopy(NoCopy &&) = default;
    };
    /// Copy-only type fixture for rejecting completion values that cannot be moved.
    struct NoMove
    {
        NoMove(NoMove const &) = default;
        NoMove(NoMove &&) = delete;
    };
    /// Payload fixture for rejecting completion values with potentially throwing destruction.
    struct ThrowingDestructor
    {
        ~ThrowingDestructor() noexcept(false) {}
    };
    static_assert(validate_completion(^^int const &).empty());
    static_assert(!validate_completion(^^Incomplete &).empty());
    static_assert(!validate_completion(^^NoCopy const &).empty());
    static_assert(!validate_completion(^^NoMove &).empty());
    static_assert(!validate_completion(^^ThrowingDestructor).empty());
    static_assert(!validate_expected_value(^^std::in_place_t).empty());
    static_assert(!validate_expected_value(^^std::unexpect_t).empty());
    static_assert(!validate_expected_value(^^std::unexpected<int>).empty());
    static_assert(validate_expected_value(^^std::tuple<std::in_place_t>).empty());
    static_assert(validate_expected_value(^^std::variant<std::unexpect_t, int>).empty());
}

#endif  // DOCTEST_CONFIG_DISABLE
