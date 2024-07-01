// Copyright (c) 2022 Mohammad Nejati
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <oneshot.hpp>

#include <boost/asio.hpp>

#include <iostream>
#include <string>

namespace asio = boost::asio;

asio::awaitable<void> sender_task(oneshot::sender<std::string> sender)
{
    auto timer = asio::steady_timer{ co_await asio::this_coro::executor };

    for (auto i = 1; i <= 3; i++)
    {
        timer.expires_after(std::chrono::seconds{ 1 });
        co_await timer.async_wait(asio::deferred);
        std::cout << i << '\n';
    }

    sender.send("HOWDY!");
}

asio::awaitable<void> receiver_task(oneshot::receiver<std::string> receiver)
{
    std::cout << "Waiting for sender...\n";
    co_await receiver.async_wait(asio::deferred);
    std::cout << "The result: " << receiver.get() << '\n';
}

int main()
{
    auto ctx = asio::io_context{};

    auto [s, r] = oneshot::create<std::string>();

    asio::co_spawn(ctx, sender_task(std::move(s)), asio::detached);
    asio::co_spawn(ctx, receiver_task(std::move(r)), asio::detached);

    ctx.run();
}
