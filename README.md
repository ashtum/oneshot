## What is oneshot?

**oneshot** is a single-header Asio-based scheduler-aware and thread-safe channel that does not block an entire thread when waiting on the receiver side. Instead, it cooperates with Asio's executor like other Asio-based io_objects (e.g., `asio::steady_timer`).

By utilizing the fact that the channel can only transport a single value, the implementation is more efficient and lightweight than a reusable channel.

* The sender side never needs to wait.
* `oneshot::sender<T>` and `oneshot::receiver<T>` types are move-only.
* oneshot is thread-safe by default, meaning the sender and receiver parts can reside on different threads.
* `oneshot::sender<void>` and `oneshot::receiver<void>` can be used to carry a signal.
* `oneshot::create<T>()` can take a user-provided allocator for allocating the shared state.

### Quick usage

The latest version of the single header can be downloaded from [`include/oneshot.hpp`](include/oneshot.hpp).
```c++
//#define ONESHOT_ASIO_STANDALONE for stand-alone version of Asio
#include <oneshot.hpp>

#include <boost/asio.hpp>

#include <iostream>

namespace asio = boost::asio;

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

asio::awaitable<void>
receiver_task(oneshot::receiver<std::string> receiver)
{
    std::cout << "Waiting on sender...\n";
    co_await receiver.async_wait();
    std::cout << "The result: " << receiver.get() << '\n';
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

```

Output:

```BASH
Waiting on sender...
1
2
3
The result: HOWDY!
```

#### Custom allocator
Because oneshot uses type-erased deleter for its shared state, using custom allcoator doesn't change sender and receiver types.

```C++
auto [s, r] = oneshot::create<int, asio::recycling_allocator<int>>();

static_assert(std::is_same_v<decltype(s), oneshot::sender<int>>);
static_assert(std::is_same_v<decltype(r), oneshot::receiver<int>>);
```

#### Sender and Receiver are lightweight handlers (8 bytes on 64bit machines)

```C++
static_assert(sizeof(oneshot::sender<std::string>) == sizeof(void*));
static_assert(sizeof(oneshot::receiver<std::string>) == sizeof(void*));
```

#### Small shared state
oneshot uses atomic operation for thread safety, and doesn't need to carry an instance of `std::mutex`, also it uses internal refcounting for life time management instead of using `std::shared_ptr`, these made the footprint of shared state very small (24 bytes on 64but machines for `T`s that are less than 8 bytes).
```C++
static_assert(sizeof(oneshot::detail::shared_state<int>) == 3 * sizeof(void*));
```
