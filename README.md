## What is the oneshot?

**oneshot** is a single-header Asio based scheduler aware and thread-safe channel that does not block a whole thread if you want to wait on receiver side. instead it cooperates with Asio's executor like other Asio based io_objects (e.g. asio::steady_timer).  

By utilizing the fact the channel can only transport a single value, the implementation is more efficent and lightweight than a reusable channel.  

* Sender side never needs to wait.
* `oneshot::sender<T>` and `oneshot::receiver<T>` types are move-only.
* oneshot is thread-safe by default, that means sender and receiver parts can reside on different threads.
* `oneshot::sender<void>` and `oneshot::receiver<void>` can be used to carry signal.
* `oneshot::create<T>()` can take user provided allocator for allocating the shared state.

### Quick usage

The latest version of the single header can be downloaded from [`include/oneshot.hpp`](include/oneshot.hpp).  
```c++
//#define ONESHOT_ASIO_STANDALONE for stand-alone version of Asio
#include <oneshot.hpp>

#include <boost/asio.hpp>
#include <fmt/format.h>

asio::awaitable<void> sender_task(oneshot::sender<std::string> s)
{
    s.send("HOWDY!");
    co_return;
}

asio::awaitable<void> receiver_task(oneshot::receiver<std::string> r)
{
    co_await r.async_wait(asio::deferred);
    fmt::print("The result: {}\n", r.get());
}

int main()
{
    auto ctx = asio::io_context{};

    auto [s, r] = oneshot::create<std::string>();

    asio::co_spawn(ctx, sender_task(std::move(s)), asio::detached);
    asio::co_spawn(ctx, receiver_task(std::move(r)), asio::detached);

    ctx.run();
}
```

Output:

```BASH
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
