// Copyright (c) 2022 Mohammad Nejati
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <oneshot.hpp>

#include <boost/asio.hpp>
#include <boost/test/unit_test.hpp>

#include <memory_resource>

BOOST_AUTO_TEST_SUITE(oneshot)

namespace asio = boost::asio;

BOOST_AUTO_TEST_CASE(no_state)
{
    auto [s, r] = oneshot::create<std::string>();
    auto s2     = std::move(s);
    auto r2     = std::move(r);

    BOOST_CHECK_EXCEPTION(
        s.send(""),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });

    BOOST_CHECK_EXCEPTION(
        r.is_ready(),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });

    BOOST_CHECK_EXCEPTION(
        r.get(),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });

    asio::io_context ctx;
    BOOST_CHECK_EXCEPTION(
        r.async_wait(asio::bind_executor(ctx, [](auto) {})),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });

    BOOST_CHECK_EXCEPTION(
        std::move(r).async_extract(asio::bind_executor(ctx, [](auto, auto) {})),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });
}

BOOST_AUTO_TEST_CASE(custom_allocator)
{
    auto [s, r] = oneshot::
        create<std::string, std::pmr::polymorphic_allocator<std::string>>(
            std::pmr::new_delete_resource());
}

BOOST_AUTO_TEST_CASE(get)
{
    auto [s, r] = oneshot::create<std::string>();

    BOOST_CHECK(!r.is_ready());
    s.send("Hello");
    BOOST_CHECK_EQUAL(r.get(), "Hello");
    BOOST_CHECK(r.is_ready());
}

BOOST_AUTO_TEST_CASE(get_unready)
{
    auto [s, r] = oneshot::create<std::string>();

    BOOST_CHECK(!r.is_ready());
    BOOST_CHECK_EXCEPTION(
        r.get(),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::unready; });
}

BOOST_AUTO_TEST_CASE(send_value)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(r.get(), "Hello");
            }));

    s.send("Hello");

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(send_void)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<void>();
    auto called = 0;

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK(!ec);
            }));

    s.send();

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(async_extract_send_void)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<void>();
    auto called = 0;

    std::move(r).async_extract(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK(!ec);
            }));

    s.send();

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(wait_after_send)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    s.send("Hello");

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(r.get(), "Hello");
            }));

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(broken_sender)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    {
        auto s2 = std::move(s);
    } // destroys promise

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK_EQUAL(ec, oneshot::errc::broken_sender);
                BOOST_CHECK_EXCEPTION(
                    r.get(),
                    oneshot::error,
                    [](const auto& e)
                    { return e.code() == oneshot::errc::unready; });
            }));

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(duplicate_wait_on_receiver)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(r.get(), "Hello");
            }));

    r.async_wait(
        asio::bind_executor(
            ctx,
            [&](auto ec)
            {
                called++;
                BOOST_CHECK_EQUAL(
                    ec, oneshot::errc::duplicate_wait_on_receiver);
                BOOST_CHECK_EQUAL(r.get(), "Hello");
            }));

    s.send("Hello");

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 2);
}

BOOST_AUTO_TEST_CASE(cancellation_after_send)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    auto cs = asio::cancellation_signal{};
    r.async_wait(
        asio::bind_cancellation_slot(
            cs.slot(),
            asio::bind_executor(
                ctx,
                [&](auto ec)
                {
                    called++;
                    BOOST_CHECK(!ec);
                    BOOST_CHECK_EQUAL(r.get(), "Hello");
                })));

    s.send("Hello");
    cs.emit(asio::cancellation_type::total);

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(cancellation_before_send)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    auto cs = asio::cancellation_signal{};
    r.async_wait(
        asio::bind_cancellation_slot(
            cs.slot(),
            asio::bind_executor(
                ctx,
                [&](auto ec)
                {
                    called++;
                    BOOST_CHECK_EQUAL(ec, oneshot::errc::cancelled);
                })));

    cs.emit(asio::cancellation_type::total);
    s.send("Hello");

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(async_extract_send_value)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    std::move(r).async_extract(
        asio::bind_executor(
            ctx,
            [&](auto ec, std::string v)
            {
                called++;
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(v, "Hello");
            }));

    BOOST_CHECK_EXCEPTION(
        r.get(),
        oneshot::error,
        [](const auto& e) { return e.code() == oneshot::errc::no_state; });

    s.send("Hello");

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(async_extract_move_only)
{
    class move_only
    {
      public:
        move_only() = default; // necessary for async interface
        move_only(int)
        {
        }
        move_only(const move_only&) = delete;
        move_only(move_only&&)      = default;
        move_only&
        operator=(const move_only&) = delete;
        move_only&
        operator=(move_only&&) = delete;
    };
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<move_only>();
    auto called = 0;

    std::move(r).async_extract(
        asio::bind_executor(
            ctx,
            [&](auto ec, move_only)
            {
                called++;
                BOOST_CHECK(!ec);
            }));

    s.send(1);

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(async_extract_cancellation_before_send)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<std::string>();
    auto called = 0;

    auto cs = asio::cancellation_signal{};
    std::move(r).async_extract(
        asio::bind_cancellation_slot(
            cs.slot(),
            asio::bind_executor(
                ctx,
                [&](auto ec, auto)
                {
                    called++;
                    BOOST_CHECK_EQUAL(ec, oneshot::errc::cancelled);
                })));

    cs.emit(asio::cancellation_type::terminal);

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_CASE(async_extract_cancellation_before_send_void)
{
    auto ctx    = asio::io_context{};
    auto [s, r] = oneshot::create<void>();
    auto called = 0;

    auto cs = asio::cancellation_signal{};
    std::move(r).async_extract(
        asio::bind_cancellation_slot(
            cs.slot(),
            asio::bind_executor(
                ctx,
                [&](auto ec)
                {
                    called++;
                    BOOST_CHECK_EQUAL(ec, oneshot::errc::cancelled);
                })));

    cs.emit(asio::cancellation_type::terminal);

    BOOST_CHECK_EQUAL(called, 0);
    ctx.run();
    BOOST_CHECK_EQUAL(called, 1);
}

BOOST_AUTO_TEST_SUITE_END()
