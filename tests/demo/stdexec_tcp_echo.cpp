// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

/**
 * @file stdexec_tcp_echo.cpp
 *
 * Demonstrates stdexec interoperability with a concurrent TCP echo exchange.
 *
 * ReaderIO sequence forks client and server libfork tasks, whose coroutines use
 * await_sender to await standalone Asio operations exposed as stdexec senders.
 */

#include <cstddef>
#include <cstdio>
#include <exception>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <asio/buffer.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <exec/asio/use_sender.hpp>
#include <libfork/core/task.hpp>
#include <libfork/schedule/lazy_pool.hpp>

#include <fnfelt/interop/stdexec.hpp>
#include <fnfelt/monad/reader_io.hpp>

namespace reader_io = fnfelt::monad::reader_io;
using exec::asio::use_sender;
using fnfelt::interop::await_sender;
using tcp = asio::ip::tcp;

namespace
{

/** Runs Asio completions on a separate thread until the exchange has finished. */
struct IoService
{
    asio::io_context context;
    tcp::acceptor listener{context, tcp::endpoint{asio::ip::address_v4::loopback(), 0}};

    // Keep run() alive while libfork tasks have not yet submitted I/O or are between operations.
    asio::executor_work_guard<asio::io_context::executor_type> work =
        asio::make_work_guard(context);

    std::jthread thread{[this] { context.run(); }};

    ~IoService()
    {
        // Let run() return after completing outstanding work, then join its thread.
        work.reset();
        thread.join();
    }
};

/** ReaderIO environment providing the external I/O context and listener. */
struct Runtime
{
    asio::io_context & context;
    tcp::acceptor & listener;
};

constexpr std::string_view message = "Hello from fnfelt!";

constexpr auto echo_server = [](auto self, Runtime const & runtime) -> lf::task<std::size_t>
{
    tcp::socket socket(runtime.context);
    // Accept connection.
    (co_await await_sender(self.context(), runtime.listener.async_accept(socket, use_sender)))
        .value()   // std::optional::value - throw if stopped/cancelled.
        .value();  // std::expected::value - throw if error.

    std::string buffer(message.size(), '\0');

    // Read message.
    (co_await await_sender(
         self.context(), asio::async_read(socket, asio::buffer(buffer), use_sender)))
        .value()
        .value();

    // Echo message.
    co_return(
        co_await await_sender(
            self.context(), asio::async_write(socket, asio::buffer(buffer), use_sender)))
        .value()
        .value();
};

constexpr auto echo_client =
    [](auto self, Runtime const & runtime, tcp::endpoint const endpoint) -> lf::task<std::string>
{
    tcp::socket client_socket(runtime.context);

    // Connect to the server.
    (co_await await_sender(self.context(), client_socket.async_connect(endpoint, use_sender)))
        .value()   // std::optional::value - throw if stopped/cancelled.
        .value();  // std::expected::value - throw if error.

    // Send message.
    (co_await await_sender(
         self.context(), asio::async_write(client_socket, asio::buffer(message), use_sender)))
        .value()
        .value();

    std::string reply(message.size(), '\0');

    // Read reply.
    (co_await await_sender(
         self.context(), asio::async_read(client_socket, asio::buffer(reply), use_sender)))
        .value()
        .value();
    co_return reply;
};
}  // namespace

int main()
{
    try
    {
        IoService service;
        auto const endpoint = service.listener.local_endpoint();

        // sequence forks both actions. ReaderIO supplies the Asio runtime, and libfork
        // resumes each coroutine after its socket sender completes on Asio's thread.
        auto exchange = reader_io::sequence<Runtime>(
            reader_io::create_async<Runtime>(echo_server),
            reader_io::create_async<Runtime>(echo_client, endpoint));
        auto const [written, reply] =
            std::move(exchange)
                .build(Runtime{.context = service.context, .listener = service.listener})
                .sync_wait(lf::lazy_pool{2});

        std::println("TCP echo: {} ({} bytes)", reply, written);
        return reply == message && written == message.size() ? 0 : 1;
    }
    catch (std::exception const & error)
    {
        std::println(stderr, "TCP echo failed: {}", error.what());
        return 1;
    }
}
