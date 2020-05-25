/**
 * Copyright 2020 (c) Tomasz Kamiński
 *
 * Use, modification, and distribution is subject to the Boost Software
 * License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 *    Authors: Tomasz Kamiński (tomaszkam@gmail.com)
 */

#ifndef GODBOLT_COMPATIBLE
#pragma once
#include "concepts.hpp"
#include "capture_sender.hpp"
#endif // GODBOLT_COMPATIBLE

template<typename Receiver>
struct resume_via_visitor
{
    Receiver&& r;
    
    template<typename... Args>
    void operator()(received_values<Args...>& v)
    {
        std::apply([&r=r](auto&&... args) {
          std::move(r).set_value(std::forward<decltype(args)>(args)...);
        }, std::move(v.value));
    }
    
    template<typename Arg>
    void operator()(received_error<Arg>& v) noexcept
    {
       std::move(r).set_error(std::move(v).value);
    }
    
    void operator()(received_done) noexcept
    {
       std::move(r).set_done();
    }
};

template<receiver Receiver, typename OpState>
struct resume_via_receiver
{
    Receiver r;
    OpState* opState;
    
    void set_value() &&
    {
        opState->nested_operation = std::nullopt;
        resume_via_visitor<Receiver> visitor(std::move(r));
        try
        {
           std::visit(visitor, *opState->store);
        }
        catch(...)
        {
           std::move(r).set_error(std::current_exception());
        }
    };
    
    template<typename Error>
    void set_error(Error&& err) &&
    {
      std::move(r).set_error(std::forward<Error>(err));
    }
    
    void set_done() && {
      std::move(r).set_done();
    }
};

template<scheduler Scheduler, typename ReceivedArgs>
struct resume_via_sender
{
    Scheduler sched;
    ReceivedArgs* store;
    
    using Sender = decltype(std::declval<Scheduler>().schedule());
    
    template<typename Receiver>
    // ?  requires sender_to<Sender, Receiver>
    friend auto connect(resume_via_sender wrap, Receiver&& r)
    {
       using decayed_receiver = std::remove_cvref_t<Receiver>;

       struct operation_type
       {
          ReceivedArgs* store;
          decayed_receiver receiver;

          using nested_receiver_type = resume_via_receiver<decayed_receiver, operation_type>;
          using nested_operation_type = operation_state_type<Sender, nested_receiver_type>;
          std::optional<nested_operation_type> nested_operation;
        
          explicit operation_type(resume_via_sender&& wrap, Receiver&& r)
            : store(wrap.store), nested_operation(std::in_place, init_from_invoke{[&] { 
               return connect(std::move(wrap.sched).schedule(), nested_receiver_type{std::forward<Receiver>(r), this});
             }})
          {}
        
          operation_type(operation_type&&) = delete;
          
          void start() && {
             return std::move(*nested_operation).start();
          }
       };
       
       return operation_type(std::move(wrap), std::forward<Receiver>(r));
    };

    Scheduler scheduler() const
    {
       return sched;
    }
};

template<scheduler Scheduler, typename ReceivedArgs>
resume_via_sender<std::remove_cvref_t<Scheduler>, ReceivedArgs> resume_via(Scheduler&& sched, ReceivedArgs& store)
{
  return {std::forward<Scheduler>(sched), &store};
}
