#ifndef GODBOLT_COMPATIBLE
#pragma once
#include "concepts.hpp"
#endif // GODBOLT_COMPATIBLE

#include <functional>
#include <utility>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <type_traits>

struct poor_void_invocable_interface
{
    poor_void_invocable_interface(poor_void_invocable_interface&&) = delete;

    virtual void call() && noexcept = 0;
    virtual ~poor_void_invocable_interface() = default;
};

template<typename T>
struct poor_void_invocable_impl : poor_void_invocable_interface
{
    template<typename... Args>
    explicit poor_void_invocable_impl(std::in_place_t, Args&&... args)
      : t(std::forward<Args>(args)...)
    {}

    void call() && noexcept override
    {
        return std::invoke(std::move(t));
    }

private:
    T t;
};

struct void_invocable
{
    void_invocable() = default;
    template<typename T, typename... Args>
    explicit void_invocable(std::in_place_type_t<T>, Args&&... args)
      : val(std::make_unique<poor_void_invocable_impl<T>>(std::forward<Args>(args)...))
    {}

    template<typename Arg>
    explicit void_invocable(std::in_place_t, Arg&& arg)
      : void_invocable(std::in_place_type<std::decay_t<Arg>>, std::forward<Arg>(arg))
    {}

    explicit operator bool() const { return bool(val); }

    void operator()() && noexcept
    {
        std::move(*val).call();
    }

private:
    std::unique_ptr<poor_void_invocable_interface> val;
};

struct thread_pool
{
    struct scheduler_type;
    struct sender_type;

    explicit thread_pool(std::size_t n = 1)
    {
        workers.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
          workers.emplace_back(std::bind_front(&thread_pool::runWork, this));
    }

    void enque(void_invocable f)
    {
      {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.push_back(std::move(f));
      }

    }

    scheduler_type scheduler();

private:
    void runWork(std::stop_token st)
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(mutex);
            bool has_task = cv.wait(lock, st, [this] { return !tasks.empty(); });
            if (st.stop_requested())
              return;
            if (!has_task)
              continue;

            auto task = std::move(tasks.front());
            tasks.pop_front();
            lock.unlock();

            std::move(task)();
        }
    }
    
    std::condition_variable_any cv;
    std::mutex mutex;

    std::vector<std::jthread> workers;
    std::deque<void_invocable> tasks;
};

template<receiver_of Receiver>
void_invocable to_void_invocable(Receiver&& recv)
{
   return void_invocable(std::in_place, [r = std::forward<Receiver>(recv)] {
     try
     {
         std::move(r).set_value();
     } 
     catch (...)
     {
         std::move(r).set_exception();
     }
   });
}

struct thread_pool::sender_type
{
   template<template<class...> class Tuple, template<class...> class Variant>
     using value_types = Variant<Tuple<>>;
   template<template<class...> class Variant>
     using error_types = Variant<std::exception_ptr>;
   static constexpr bool sends_done = false;

   explicit sender_type(thread_pool& p) 
     : pool(&p)
   {}

   template<typename Receiver>
     requires receiver_of<Receiver>
   friend auto connect(sender_type s, Receiver&& r)
   {
      struct operation_type
      {
         void_invocable val;
         thread_pool* pool;

         void start() && {
             pool->enque(std::move(val));
         }
      };

      return operation_type{to_void_invocable(std::forward<Receiver>(r)), s.pool};
   }

   template<typename Receiver>
     requires receiver_of<Receiver>
   friend void submit(sender_type s, Receiver&& r)
   {
       s.pool->enque(to_void_invocable(std::forward<Receiver>(r)));
   }

   thread_pool::scheduler_type scheduler() const;

private:
   thread_pool* pool;
};


struct thread_pool::scheduler_type
{
   explicit scheduler_type(thread_pool& p) 
     : pool(&p)
   {}

   thread_pool::sender_type schedule() const
   {
      return thread_pool::sender_type(*pool);
   }

private:
   thread_pool* pool;
};

inline thread_pool::scheduler_type thread_pool::scheduler()
{
    return scheduler_type(*this);
}
   
inline thread_pool::scheduler_type thread_pool::sender_type::scheduler() const
{
   return thread_pool::scheduler_type(*pool);
}
