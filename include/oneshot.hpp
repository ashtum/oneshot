// Copyright (c) 2022 Mohammad Nejati
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#ifdef ONESHOT_ASIO_STANDALONE
#include <asio/append.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/compose.hpp>
#include <asio/deferred.hpp>
#include <asio/post.hpp>
namespace oneshot
{
namespace net    = asio;
using error_code = std::error_code;
} // namespace oneshot
#else
#include <boost/asio/append.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/post.hpp>
namespace oneshot
{
namespace net    = boost::asio;
using error_code = boost::system::error_code;
} // namespace oneshot
#endif

namespace oneshot
{
enum class errc
{
    no_state = 1,
    cancelled,
    unready,
    broken_sender,
    duplicate_wait_on_receiver,
};
} // namespace oneshot

namespace std
{
template<>
struct is_error_code_enum<oneshot::errc> : true_type
{
};
} // namespace std

namespace oneshot
{
inline const std::error_category&
oneshot_category()
{
    static const struct : std::error_category
    {
        const char*
        name() const noexcept override
        {
            return "oneshot";
        }

        std::string
        message(int ev) const override
        {
            switch (static_cast<errc>(ev))
            {
                case errc::no_state:
                    return "No associated state";
                case errc::cancelled:
                    return "Cancelled";
                case errc::unready:
                    return "Unready";
                case errc::broken_sender:
                    return "Broken sender";
                case errc::duplicate_wait_on_receiver:
                    return "Duplicate wait on receiver";
                default:
                    return "Unknown error";
            }
        }
    } category;

    return category;
};

inline std::error_code
make_error_code(errc e)
{
    return { static_cast<int>(e), oneshot_category() };
}

class error : public std::system_error
{
  public:
    using std::system_error::system_error;
};

namespace detail
{
struct wait_op
{
    virtual void
    shutdown() noexcept               = 0;
    virtual void complete(error_code) = 0;
    virtual ~wait_op()                = default;
};

template<class Executor, class Handler>
class wait_op_model final : public wait_op
{
    net::executor_work_guard<Executor> work_guard_;
    Handler handler_;

  public:
    wait_op_model(Executor e, Handler handler)
        : work_guard_(std::move(e))
        , handler_(std::move(handler))
    {
    }

    [[nodiscard]] auto
    get_cancellation_slot() const noexcept
    {
        return net::get_associated_cancellation_slot(handler_);
    }

    static wait_op_model*
    construct(Executor e, Handler handler)
    {
        auto halloc = net::get_associated_allocator(handler);
        auto alloc  = typename std::allocator_traits<
            decltype(halloc)>::template rebind_alloc<wait_op_model>(halloc);
        using traits = std::allocator_traits<decltype(alloc)>;
        auto pmem    = traits::allocate(alloc, 1);

        try
        {
            return new (pmem) wait_op_model(std::move(e), std::move(handler));
        }
        catch (...)
        {
            traits::deallocate(alloc, pmem, 1);
            throw;
        }
    }

    static void
    destroy(wait_op_model* self, net::associated_allocator_t<Handler> halloc)
    {
        auto alloc = typename std::allocator_traits<
            decltype(halloc)>::template rebind_alloc<wait_op_model>(halloc);
        self->~wait_op_model();
        auto traits = std::allocator_traits<decltype(alloc)>();
        traits.deallocate(alloc, self, 1);
    }

    void
    complete(error_code ec) override
    {
        net::post(
            work_guard_.get_executor(),
            [this, ec]()
            {
                get_cancellation_slot().clear();
                auto g = std::move(work_guard_);
                auto h = std::move(handler_);
                destroy(this, net::get_associated_allocator(h));
                std::move(h)(ec);
            });
    }

    void
    shutdown() noexcept override
    {
        get_cancellation_slot().clear();
        destroy(this, net::get_associated_allocator(this->handler_));
    }
};

template<typename T>
struct storage;

template<>
struct storage<void>
{
    void
    construct() noexcept
    {
    }

    void
    destroy() noexcept
    {
    }

    void*
    object() noexcept
    {
        return nullptr;
    }
};

template<typename T>
struct storage
{
    alignas(T) char payload_[sizeof(T)];

    template<typename... Args>
    void
    construct(Args&&... args)
    {
        new (&payload_) T(std::forward<Args>(args)...);
    }

    void
    destroy() noexcept
    {
        std::destroy_at(object());
    }

    T*
    object() noexcept
    {
        return std::launder(reinterpret_cast<T*>(&payload_));
    }
};

template<typename T>
class shared_state
{
    enum : uint8_t
    {
        empty    = 0,
        engaged  = 1,
        waiting  = 2,
        sent     = 3,
        detached = 4
    };

    std::atomic<uint8_t> state_{ empty };
    [[no_unique_address]] storage<T> storage_;
    void (*deleter_)(shared_state*){ nullptr };
    wait_op* wait_op_{ nullptr };

  public:
    shared_state(void (*deleter)(shared_state*)) noexcept
        : deleter_{ deleter }
    {
    }

    shared_state(const shared_state&) = delete;
    shared_state(shared_state&&)      = delete;

    template<typename... Args>
    void
    send(Args&&... args)
    {
        storage_.construct(std::forward<Args>(args)...);

        // possible vals: empty, waiting, detached(receiver)
        auto prev = state_.fetch_add(1, std::memory_order_release);

        if (prev == detached)
        {
            storage_.destroy();
            return deleter_(this);
        }

        if (prev == waiting)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            wait_op_->complete({});
        }
    }

    void
    sender_detached() noexcept
    {
        // possible vals: empty, waiting, detached(receiver)
        auto prev = state_.exchange(detached, std::memory_order_relaxed);

        if (prev == detached)
            return deleter_(this);

        if (prev == waiting)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            wait_op_->complete(errc::broken_sender);
        }
    }

    template<typename CompletionToken>
    auto
    async_wait(CompletionToken&& token)
    {
        return net::async_initiate<decltype(token), void(error_code)>(
            [this](auto handler)
            {
                auto exec = net::get_associated_executor(handler);

                using handler_type = std::decay_t<decltype(handler)>;
                using model_type  = wait_op_model<decltype(exec), handler_type>;
                model_type* model = model_type ::construct(
                    std::move(exec), std::forward<decltype(handler)>(handler));
                auto c_slot = model->get_cancellation_slot();
                if (c_slot.is_connected())
                {
                    c_slot.assign(
                        [this](net::cancellation_type type)
                        {
                            if (type != net::cancellation_type::none)
                            {
                                // possible vals: waiting, sent,
                                // detached(sender)
                                auto prev = state_.exchange(
                                    empty, std::memory_order_relaxed);

                                if (prev == waiting)
                                {
                                    wait_op_->complete(errc::cancelled);
                                    wait_op_ = nullptr;
                                }
                                else // prev has been sent or detached(sender)
                                {
                                    state_.store(
                                        prev, std::memory_order_relaxed);
                                }
                            }
                        });
                }

                if (wait_op_)
                    return model->complete(errc::duplicate_wait_on_receiver);

                wait_op_ = model;

                // possible vals: empty, engaged, detached(sender)
                auto prev = state_.exchange(waiting, std::memory_order_release);

                if (prev == detached)
                {
                    state_.store(prev, std::memory_order_relaxed);
                    model->complete(errc::broken_sender);
                }
                else if (prev == engaged)
                {
                    state_.store(prev, std::memory_order_relaxed);
                    model->complete({});
                }
            },
            token);
    }

    void
    receiver_detached() noexcept
    {
        // possible vals: empty, engaged, sent, detached(sender)
        auto prev = state_.exchange(detached, std::memory_order_relaxed);

        if (prev == detached)
            return deleter_(this);

        if (prev == engaged || prev == sent)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            storage_.destroy();
            return deleter_(this);
        }
    }

    bool
    is_ready() const noexcept
    {
        auto state = state_.load(std::memory_order_relaxed);
        return state == sent || state == engaged;
    }

    T*
    get_stored_object() noexcept
    {
        auto state = state_.load(std::memory_order_acquire);

        if (state == sent || state == engaged)
            return storage_.object();

        return nullptr;
    }
};

template<typename T>
struct async_extract_signature
{
    using type = void(error_code, T);
};

template<>
struct async_extract_signature<void>
{
    using type = void(error_code);
};

template<typename T>
using async_extract_signature_t = typename async_extract_signature<T>::type;

template<typename T, class X>
class shs_handle
{
    shared_state<T>* shared_state_{ nullptr };

  public:
    shs_handle() noexcept = default;
    shs_handle(shared_state<T>* shared_state) noexcept
        : shared_state_{ shared_state }
    {
    }

    shs_handle(shs_handle&& other) noexcept
    {
        std::swap(shared_state_, other.shared_state_);
    }

    shs_handle&
    operator=(shs_handle&& other) noexcept
    {
        std::swap(shared_state_, other.shared_state_);
        return *this;
    }

    operator bool() const noexcept
    {
        return shared_state_ != nullptr;
    }

    shared_state<T>*
    operator->() const noexcept
    {
        return shared_state_;
    }

    shared_state<T>*
    release() noexcept
    {
        return std::exchange(shared_state_, nullptr);
    }

    ~shs_handle()
    {
        if (shared_state_)
            X::detach(shared_state_);
    }
};

template<typename T>
struct sender_shs_handle : shs_handle<T, sender_shs_handle<T>>
{
    static void
    detach(shared_state<T>* p) noexcept
    {
        p->sender_detached();
    }
};

template<typename T>
struct receiver_shs_handle : shs_handle<T, receiver_shs_handle<T>>
{
    static void
    detach(shared_state<T>* p) noexcept
    {
        p->receiver_detached();
    }
};

} // namespace detail

template<typename T>
class sender
{
    detail::sender_shs_handle<T> shs_handle_;

  public:
    sender() noexcept = default;

    sender(detail::shared_state<T>* shared_state) noexcept
        : shs_handle_{ shared_state }
    {
    }

    template<typename... Args>
    void
    send(Args&&... args)
    {
        if (!shs_handle_)
            throw error{ errc::no_state };

        shs_handle_->send(std::forward<Args>(args)...);
        shs_handle_.release();
    }
};

template<typename T>
class receiver
{
    detail::receiver_shs_handle<T> shs_handle_;

  public:
    receiver() noexcept = default;

    receiver(detail::shared_state<T>* shared_state) noexcept
        : shs_handle_{ shared_state }
    {
    }

    template<typename CompletionToken = net::deferred_t>
    auto
    async_extract(CompletionToken&& token = CompletionToken{}) &&
    {
        if (!shs_handle_)
            throw error{ errc::no_state };

        return net::async_compose<
            CompletionToken,
            detail::async_extract_signature_t<T>>(
            [shs_handle = std::move(shs_handle_),
             init       = false](auto&& self, error_code ec = {}) mutable
            {
                if (!std::exchange(init, true))
                    return shs_handle->async_wait(std::move(self));

                if (ec)
                {
                    if constexpr (std::is_same_v<T, void>)
                        return self.complete(ec);
                    else
                        return self.complete(ec, T{});
                }

                if constexpr (std::is_same_v<T, void>)
                {
                    return self.complete(ec);
                }
                else
                {
                    if (auto* p = shs_handle->get_stored_object())
                        return self.complete(ec, std::move(*p));
                    else
                        return self.complete(errc::unready, T{});
                }
            },
            token);
    }

    template<typename CompletionToken = net::deferred_t>
    auto
    async_wait(CompletionToken&& token = CompletionToken{})
    {
        if (!shs_handle_)
            throw error{ errc::no_state };

        return shs_handle_->async_wait(std::forward<CompletionToken>(token));
    }

    bool
    is_ready() const
    {
        if (!shs_handle_)
            throw error{ errc::no_state };

        return shs_handle_->is_ready();
    }

    decltype(auto)
    get() const
    {
        static_assert(!std::is_same_v<T, void>, "Only for non void receivers");

        if (!shs_handle_)
            throw error{ errc::no_state };

        if (auto* p = shs_handle_->get_stored_object())
            return std::add_lvalue_reference_t<T>(*p);

        throw error{ errc::unready };
    }
};

template<typename T, typename Allocator = std::allocator<T>>
inline std::pair<sender<T>, receiver<T>>
create(Allocator alloc = {})
{
    struct wrapper
    {
        detail::shared_state<T> shared_state_;
        [[no_unique_address]] Allocator alloc_;

        wrapper(void (*deleter)(detail::shared_state<T>*), Allocator alloc)
            : shared_state_{ deleter }
            , alloc_{ alloc }
        {
        }
    };

    using r_alloc_t = typename std::allocator_traits<
        Allocator>::template rebind_alloc<wrapper>;
    using traits_t = std::allocator_traits<r_alloc_t>;
    auto r_alloc   = r_alloc_t{ alloc };
    auto* p        = traits_t::allocate(r_alloc, 1);
    auto* deleter  = +[](detail::shared_state<T>* shared_state)
    {
        auto* p      = reinterpret_cast<wrapper*>(shared_state);
        auto r_alloc = r_alloc_t{ p->alloc_ }; // copy before destroy
        traits_t::destroy(r_alloc, p);
        traits_t::deallocate(r_alloc, p, 1);
    };
    traits_t::construct(r_alloc, p, deleter, alloc); // noexcept
    return { &p->shared_state_, &p->shared_state_ };
}
} // namespace oneshot
