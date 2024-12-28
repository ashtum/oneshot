// Copyright (c) 2022 Mohammad Nejati
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <oneshot.hpp>

#include <boost/asio.hpp>

#include <iostream>

namespace asio = boost::asio;

asio::awaitable<void>
receiver_task(oneshot::receiver<std::string> receiver)
{
    std::cout << "Waiting on sender...\n";
    std::cout << co_await std::move(receiver).async_extract() << '\n';
    // Use receiver.async_wait() and receiver.get() if T is not
    // DefaultConstructible or MoveConstructible.
}

asio::awaitable<void>
sender_task(oneshot::sender<std::string> sender)
{
    auto timer = asio::steady_timer{ co_await asio::this_coro::executor };

    for (auto i = 1; i <= 3; i++)
    {
        timer.expires_after(std::chrono::seconds{ 1 });
        co_await timer.async_wait();
        std::cout << i << '\n';
    }

    sender.send("HOWDY!");
}

int
main()
{
    auto ctx = asio::io_context{};

    auto [sender, receiver] = oneshot::create<std::string>();

    asio::co_spawn(ctx, sender_task(std::move(sender)), asio::detached);
    asio::co_spawn(ctx, receiver_task(std::move(receiver)), asio::detached);

    ctx.run();
}
