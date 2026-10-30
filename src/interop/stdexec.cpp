// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
#ifndef DOCTEST_CONFIG_DISABLE

#include <fnfelt/interop/stdexec.hpp>

#include <doctest/doctest.h>

#include <exception>
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

/**
 * Operation lifetime counters for detecting missing or repeated cleanup.
 */
struct OperationCounts
{
    int constructed = 0;
    int destroyed = 0;
};

/**
 * Sender fixture with chosen completion signatures and programmable completion behavior.
 *
 * Supports inline and external completion, connection failure, and operation lifetime checks.
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
/**
 * Receiver environment fixture exposing a stop token and a successful result value.
 */
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
}  // namespace

TEST_CASE("stdexec bridge normalizes inline success completions")
{
    auto empty = run_sender(stdexec::just());
    using empty_outcome = std::optional<
        std::expected<std::monostate, std::expected<std::monostate, std::exception_ptr>>>;
    static_assert(std::is_same_v<decltype(empty), empty_outcome>);
    REQUIRE(empty.has_value());
    CHECK(empty->has_value());

    auto single = run_sender(stdexec::just(42));
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
    REQUIRE(failed.has_value());
    REQUIRE_FALSE(failed->has_value());
    REQUIRE(failed->error().has_value());
    CHECK_EQ(*failed->error(), error);

    CHECK_FALSE(run_sender(stdexec::just_stopped()).has_value());
}

TEST_CASE("stdexec bridge moves owned results")
{
    auto result = run_sender(stdexec::just(std::make_unique<int>(42)));
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK_EQ(***result, 42);
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

TEST_CASE("stdexec bridge distinguishes storage failure from sender error")
{
    using value_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(ThrowingPayload const &)>;
    using error_signatures =
        stdexec::completion_signatures<stdexec::set_error_t(ThrowingPayload const &)>;
    int live = 0;
    ThrowingPayload const payload{&live, true, false};

    SUBCASE("success storage throws")
    {
        OperationCounts counts;
        auto result = run_sender(
            test_sender<value_signatures>(
                [&payload](auto receiver) { stdexec::set_value(std::move(receiver), payload); },
                &counts));
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->has_value());
        REQUIRE_FALSE(result->error().has_value());
        CHECK_THROWS_WITH_AS(
            std::rethrow_exception(result->error().error()), "copy failed", std::runtime_error);
        CHECK_EQ(counts.destroyed, 1);
    }

    SUBCASE("error storage throws")
    {
        auto result = run_sender(
            test_sender<error_signatures>([&payload](auto receiver)
                                          { stdexec::set_error(std::move(receiver), payload); }));
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->has_value());
        REQUIRE_FALSE(result->error().has_value());
        CHECK_THROWS_WITH_AS(
            std::rethrow_exception(result->error().error()), "copy failed", std::runtime_error);
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
        using error_type = typename decltype(result)::value_type::error_type::value_type;
        static_assert(std::variant_size_v<value_type> == 4);
        static_assert(std::variant_size_v<error_type> == 2);
        REQUIRE(result.has_value());
        if (mode < 7)
        {
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
            REQUIRE_FALSE(result->has_value());
            REQUIRE(result->error().has_value());
            if (mode == 8)
            {
                CHECK_EQ(std::get<std::string>(*result->error()), "failed");
            }
            else
            {
                CHECK_EQ(std::get<std::error_code>(*result->error()), error);
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
    /**
     * ReaderIO state carrying receiver properties through an asynchronous composition.
     */
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
    CHECK_FALSE(program.build(runtime).sync_wait(lf::lazy_pool{2}).has_value());

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
    /**
     * Incomplete type fixture for rejecting completion arguments without a complete stored type.
     */
    struct Incomplete;
    /**
     * Move-only type fixture for rejecting completion references that require copying.
     */
    struct NoCopy
    {
        NoCopy(NoCopy const &) = delete;
        NoCopy(NoCopy &&) = default;
    };
    /**
     * Copy-only type fixture for rejecting completion values that cannot be moved.
     */
    struct NoMove
    {
        NoMove(NoMove const &) = default;
        NoMove(NoMove &&) = delete;
    };
    /**
     * Payload fixture for rejecting completion values with potentially throwing destruction.
     */
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
