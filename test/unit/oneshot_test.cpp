// Copyright (c) 2022 Mohammad Nejati
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <oneshot.hpp>

#include <boost/asio.hpp>
#include <boost/test/unit_test.hpp>

#include <memory>
#include <memory_resource>

BOOST_AUTO_TEST_SUITE(oneshot)

namespace asio = boost::asio;

struct alloc_counter
{
    int allocs   = 0;
    int deallocs = 0;
};

template<typename T>
struct counting_allocator
{
    using value_type = T;

    alloc_counter* counter;

    explicit counting_allocator(alloc_counter* c) noexcept
        : counter{ c }
    {
    }

    template<typename U>
    counting_allocator(const counting_allocator<U>& other) noexcept
        : counter{ other.counter }
    {
    }

    T*
    allocate(std::size_t n)
    {
        counter->allocs++;
        return std::allocator<T>{}.allocate(n);
    }

    void
    deallocate(T* p, std::size_t n) noexcept
    {
        counter->deallocs++;
        std::allocator<T>{}.deallocate(p, n);
    }

    template<typename U>
    bool
    operator==(const counting_allocator<U>& other) const noexcept
    {
        return counter == other.counter;
    }

    template<typename U>
    bool
    operator!=(const counting_allocator<U>& other) const noexcept
    {
        return counter != other.counter;
    }
};

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

BOOST_AUTO_TEST_CASE(pending_completion_on_destroyed_io_context)
{
    auto counter = alloc_counter{};
    auto [s, r]  = oneshot::create<std::string>();
    auto called  = 0;
    auto token   = std::make_shared<int>(0);
    auto wtoken  = std::weak_ptr<int>{ token };

    {
        auto ctx = asio::io_context{};

        r.async_wait(
            asio::bind_allocator(
                counting_allocator<char>{ &counter },
                asio::bind_executor(
                    ctx,
                    [&, token = std::move(token)](auto) { called++; })));

        s.send("Hello");

        BOOST_CHECK(!wtoken.expired());
        BOOST_CHECK_EQUAL(counter.allocs, 1);
        BOOST_CHECK_EQUAL(counter.deallocs, 0);
    }

    BOOST_CHECK_EQUAL(called, 0);
    BOOST_CHECK(wtoken.expired());
    BOOST_CHECK_EQUAL(counter.allocs, counter.deallocs);

    BOOST_CHECK(r.is_ready());
    BOOST_CHECK_EQUAL(r.get(), "Hello");
}

BOOST_AUTO_TEST_CASE(async_extract_pending_completion_on_destroyed_io_context)
{
    auto op_counter    = alloc_counter{};
    auto state_counter = alloc_counter{};
    auto called        = 0;
    auto token         = std::make_shared<int>(0);
    auto wtoken        = std::weak_ptr<int>{ token };
    auto value         = std::make_shared<int>(0);
    auto wvalue        = std::weak_ptr<int>{ value };

    {
        auto ctx = asio::io_context{};
        using state_allocator = counting_allocator<std::shared_ptr<int>>;
        auto [s, r] = oneshot::create<std::shared_ptr<int>, state_allocator>(
            state_allocator{ &state_counter });

        std::move(r).async_extract(
            asio::bind_allocator(
                counting_allocator<char>{ &op_counter },
                asio::bind_executor(
                    ctx,
                    [&, token = std::move(token)](auto, auto) { called++; })));

        s.send(std::move(value));

        BOOST_CHECK(!wtoken.expired());
        BOOST_CHECK(!wvalue.expired());
        BOOST_CHECK_EQUAL(op_counter.allocs, 1);
        BOOST_CHECK_EQUAL(op_counter.deallocs, 0);
        BOOST_CHECK_EQUAL(state_counter.allocs, 1);
        BOOST_CHECK_EQUAL(state_counter.deallocs, 0);
    }

    BOOST_CHECK_EQUAL(called, 0);
    BOOST_CHECK(wtoken.expired());
    BOOST_CHECK(wvalue.expired());
    BOOST_CHECK_EQUAL(op_counter.allocs, op_counter.deallocs);
    BOOST_CHECK_EQUAL(state_counter.allocs, state_counter.deallocs);
}

BOOST_AUTO_TEST_CASE(cancelled_pending_completion_on_destroyed_io_context)
{
    auto counter = alloc_counter{};
    auto called  = 0;
    auto cs      = asio::cancellation_signal{};

    {
        auto [s, r] = oneshot::create<std::string>();
        auto ctx    = asio::io_context{};

        r.async_wait(
            asio::bind_cancellation_slot(
                cs.slot(),
                asio::bind_allocator(
                    counting_allocator<char>{ &counter },
                    asio::bind_executor(ctx, [&](auto) { called++; }))));

        cs.emit(asio::cancellation_type::total);

        BOOST_CHECK(cs.slot().has_handler());
        BOOST_CHECK_EQUAL(counter.allocs, 1);
        BOOST_CHECK_EQUAL(counter.deallocs, 0);
    }

    BOOST_CHECK_EQUAL(called, 0);
    BOOST_CHECK_EQUAL(counter.allocs, counter.deallocs);

    BOOST_CHECK(!cs.slot().has_handler());
    cs.emit(asio::cancellation_type::total);
}

BOOST_AUTO_TEST_SUITE_END()
