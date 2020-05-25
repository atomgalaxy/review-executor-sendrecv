#pragma once

#include "concepts.hpp"
#include <iostream>

// copied from paper
template<receiver R, class F>
struct _then_receiver : R { // for exposition, inherit set_error and set_done from R
    F f_;

    // Customize set_value by invoking the callable and passing the result to the base class
    template<class... Args>
      requires receiver_of<R, std::invoke_result_t<F, Args...>>
    void set_value(Args&&... args) &&  {
        ((R&&) *this).set_value(std::invoke((F&&) f_, (Args&&) args...));
    }

    // Not shown: handle the case when the callable returns void
};

template<sender S, class F>
struct _then_sender {
    S s_;
    F f_;

    template<receiver R>
      requires sender_to<S, _then_receiver<R, F>>
    friend operation_state_type<S, _then_receiver<R, F>> connect(_then_sender s, R r) {
        return connect((S&&)s.s_, _then_receiver<R, F>{(R&&)r, (F&&)s.f_});
    }
};

template<sender S, class F>
auto then(S s, F f) {
    return _then_sender{(S&&)s, (F&&)f};
}
// end of paper

struct inline_sender
{
    template<template<class...> class Tuple, template<class...> class Variant>
      using value_types = Variant<Tuple<>>;
    template<template<class...> class Variant>
      using error_types = Variant<std::exception_ptr>;
    static constexpr bool sends_done = false;
    
    template<typename Receiver>
      requires receiver_of<Receiver>
    friend auto connect(inline_sender, Receiver&& r) {
       struct operation {
          std::remove_cvref_t<Receiver> r;
          
          void start()
          {
             try
             {
                std::move(r).set_value();
             }
             catch(...)
             {
               std::move(r).set_error(std::current_exception());
             }
          };

       };
       
                 
      return operation(std::forward<Receiver>(r));
    }
};


struct just10_sender
{
    template<template<class...> class Tuple, template<class...> class Variant>
      using value_types = Variant<Tuple<int>>;
    template<template<class...> class Variant>
      using error_types = Variant<std::exception_ptr>;
    static constexpr bool sends_done = false;
    
    template<typename Receiver>
      requires receiver_of<Receiver>
    friend auto connect(just10_sender, Receiver&& r) {
       struct operation {
          std::remove_cvref_t<Receiver> r;
          
          void start()
          {
             try
             {
                std::move(r).set_value(10);
             }
             catch(...)
             {
               std::move(r).set_error(std::current_exception());
             }
          };

       };
       
                 
      return operation(std::forward<Receiver>(r));
    }
};

struct inline_scheduler
{
  inline_sender schedule()
  {
     return {};
  }
};

struct link_error_receiver
{
   template<typename... Args>
   void set_value(Args&&...);
   
   template<typename Arg>
   void set_error(Arg&&);
   
   void set_done();
};


struct print_receiver
{
   template<typename... Args>
   void set_value(Args&&... args) 
   {
      std::cout << "got values (x" << sizeof...(Args) << "):";
      (std::cout << ... << args);
      std::cout << std::endl;
   }
   
   template<typename Arg>
   void set_error(Arg&&) { std::cout << "got error" << std::endl; }
   
   void set_done()  { std::cout << "got done" << std::endl; }
   
   inline_scheduler get_scheduler() const { return {}; };
};
