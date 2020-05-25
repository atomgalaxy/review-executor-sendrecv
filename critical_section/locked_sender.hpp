#ifndef GODBOLT_COMPATIBLE
#pragma once
#include "concepts.hpp"
#include "capture_sender.hpp"
#include "resume_via_sender.hpp"
#include "async_mutex.hpp"
#endif // GODBOLT_COMPATIBLE


template<typename Scheduler, typename Receiver, typename Operation>
struct lock_mutex_receiver
{
   Scheduler sched;
   Receiver recv;
   Operation* op;
   
   template<typename Arg>   
   void set_value(Arg& args)
   {
     op->follow_op.emplace(init_from_invoke{[this, &args] {
        return connect(resume_via(std::move(sched), args), std::move(recv));
     }});
     
     if (op->mutex->enqueue(op))
       std::move(*op->follow_op).start();
   }
};

template<typename Arg, typename...g>
using first_type = Arg;

template<typed_sender Sender, scheduler Scheduler>
struct lock_mutex_sender
{
   Sender send;
   Scheduler sched;
   async_mutex* mutex;
   handle_base** save_handle;
   
   template<typename Receiver>
     requires sender_to<Sender, Receiver>
   friend auto connect(lock_mutex_sender wrap, Receiver&& r)
   {
      using decayed_receiver = std::remove_cvref_t<Receiver>;

   
      struct operation_type : handle_base
      {
         async_mutex* mutex;
      
         using leading_sender = decltype(capture_args(std::move(send)));
         using leading_receiver = lock_mutex_receiver<Scheduler, decayed_receiver, operation_type>;
         using leading_operation = operation_state_type<leading_sender, leading_receiver>;
         leading_operation leading_op;
         
         using stored_args = typename sender_traits<leading_sender>::template value_types<first_type, first_type>;
         
         using following_sender = decltype(resume_via(std::declval<Scheduler>(), std::declval<stored_args>()));
         using following_operation = operation_state_type<following_sender, decayed_receiver>;
         std::optional<following_operation> follow_op;
         
         explicit operation_type(lock_mutex_sender&& wrap, Receiver&& r)
           : mutex(wrap.mutex),
             leading_op(
               connect(capture_args(std::move(wrap.send)), 
                      leading_receiver{std::move(wrap.sched), std::forward<Receiver>(r), this}))
           {
             *wrap.save_handle = this;
           }
          operation_type(operation_type&& other) = delete;
     
         void start() &&
         {
            return std::move(leading_op).start();
         }
         
         void run() && override
         {
           std::move(*follow_op).start();
         }
      };
      
      return operation_type(std::move(wrap), std::forward<Receiver>(r));
   }
};


template<receiver Receiver>
struct unlock_mutex_receiver
{
   Receiver recv;
   async_mutex* mutex;
   handle_base** handler;
   
   void unlock()
   {
     
      handle_base* next = mutex->deque(*handler);
      if (next)
        std::move(*next).run();
   }
   
   template<typename... Args>
   void set_value(Args&&... args) &&
   {
      unlock();
      
      try
      {
        std::move(recv).set_value(std::forward<Args>(args)...);
      }
      catch(...)
      {
        std::move(recv).set_error(std::current_exception());
      }
   }
   
   template<typename Arg>
   void set_error(Arg&& arg) && noexcept
   {
      unlock();
      std::move(recv).set_error(std::forward<Arg>(arg));
   }
  
   void set_done() && noexcept
   {
      unlock();
      std::move(recv).set_done();
   }
};

template<typed_sender Sender, typename Work>
  requires std::invocable<Work, Sender>
struct lock_sender
{
   Sender send;
   Work work;
   async_mutex* mutex;
   
   template<typename Receiver>
     requires sender_to<std::invoke_result_t<Work, Sender>, Receiver>
   friend auto connect(lock_sender wrap, Receiver&& recv)
   {
      using scheduler_type = std::remove_cvref_t<decltype(recv.get_scheduler())>;
      using locking_sender = lock_mutex_sender<Sender, scheduler_type>;
      using nested_sender = std::invoke_result_t<Work, locking_sender>;
   
      using decayed_receiver = std::remove_cvref_t<Receiver>;
      using nested_receiver = unlock_mutex_receiver<decayed_receiver>;
   
      using nested_operation = operation_state_type<nested_sender, nested_receiver>;
   
      struct operation_type 
      {
         handle_base* handle;
         nested_operation nested_op;
        
        explicit operation_type(lock_sender&& wrap, Receiver&& r)
          : handle(nullptr),
            nested_op(connect(
              std::invoke(std::move(wrap.work), 
                          locking_sender{std::move(wrap.send), r.get_scheduler(), wrap.mutex, &handle}),
                          nested_receiver{std::forward<Receiver>(r), wrap.mutex, &handle}))
        {}
           
        operation_type(operation_type&& other) = delete;
 
        void start() && 
        {
           std::move(nested_op).start();
        }
      };
       
      return operation_type(std::move(wrap), std::forward<Receiver>(recv));
   }

};

template<typed_sender Sender, typename Work>
lock_sender<std::remove_cvref_t<Sender>, std::remove_cvref_t<Work>> locked(Sender&& s, Work&& w, async_mutex& m)
{
   return {std::forward<Sender>(s), std::forward<Work>(w), &m};
}
