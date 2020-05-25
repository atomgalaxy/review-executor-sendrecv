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
#endif // GODBOLT_COMPATIBLE

#include <optional>
#include <variant>
#include <tuple>
#include <type_traits>

template<typename... Values>
struct received_values
{
   using type = std::tuple<Values...>;
   type value;
   
   explicit received_values(Values... vals) : value(std::move(vals)...)
   {}
};

template<typename Value>
struct received_error
{
   using type = Value;
   type value;
   
   explicit received_error(Value&& v) : value(std::move(v))
   {}
};

struct received_done {};

template<typename... Args>
using capture_value_tuple = received_values<std::remove_cvref_t<Args>...>;

template<typename... ValueArgs>
struct capture_value_types
{
    template<typename... ErrorArgs>
    struct capture_error_types
    {
       using with_done =
         std::variant<
           ValueArgs...,
           received_error<std::remove_cvref_t<ErrorArgs>>...,
           received_done>;
       
       using without_done =
         std::variant<
           ValueArgs...,
           received_error<std::remove_cvref_t<ErrorArgs>>...>;
        
       template<bool sends_done>
       using type = std::conditional_t<sends_done, with_done, without_done>;
    };
};

template<typed_sender S>
struct received_result
{
    using values = typename sender_traits<S>::template value_types<capture_value_tuple, capture_value_types>;
    using with_errors = typename sender_traits<S>::template error_types<values::template capture_error_types>;
    using type = typename with_errors::template type<sender_traits<S>::sends_done>;
};
  
template<typed_sender S>
using received_result_t = typename received_result<S>::type;

template<receiver Receiver, typename StoredResult>
struct capture_receiver
{
    Receiver r;
    std::optional<StoredResult>* res;
    
    template<typename... Args>
    void set_value(Args&&... args) && 
    {
        using type = received_values<std::remove_cvref_t<Args>...>;
        res->emplace(std::in_place_type<type>, std::forward<Args>(args)...);
        std::move(r).set_value(**res);
    }
    
    template<typename Arg>
    void set_error(Arg&& arg) && noexcept
    {
        using type = received_error<std::remove_cvref_t<Arg>>;
        res->emplace(std::in_place_type<type>, std::forward<Arg>(arg));
        std::move(r).set_value(**res);
    }

    void set_done() && noexcept
    {
        using type = received_done;
        res->emplace(std::in_place_type<type>);
        std::move(r).set_value(**res);
    }
};

template<typed_sender Sender>
struct capture_sender
{
    Sender s;
    using stored_result = received_result_t<Sender>;
    
    template<template<class...> class Tuple, template<class...> class Variant>
      using value_types = Variant<Tuple<stored_result&>>;
    template<template<class...> class Variant>
      using error_types = Variant<>;
    static constexpr bool sends_done = false;
    
    template<typename Receiver>
      requires sender_to<Sender, Receiver>
    friend auto connect(capture_sender wrapper, Receiver&& r) {
       using decayed_receiver = std::remove_cvref_t<Receiver>;
       using nested_receiver_type = capture_receiver<decayed_receiver, stored_result>;
       using nested_operation_type = operation_state_type<Sender, nested_receiver_type>;

       struct operation_type
       {
          std::optional<stored_result> store;
          nested_operation_type operation;
          
          explicit operation_type(Sender&& s, Receiver&& r)
            : store(), operation(connect(std::move(s), nested_receiver_type{std::forward<Receiver>(r), &store}))
          {}
          
          operation_type(operation_type&& other) = delete;
          
          void start() &&
          {
             return std::move(operation).start();
          }
       };
       
       return operation_type(std::move(wrapper.s), std::forward<Receiver>(r));
    }

    auto scheduler() const
      requires sender_with_scheduler<Sender>
    {
       return s.scheduler();
    }
};

template<sender Sender>
capture_sender<std::remove_cvref_t<Sender>> capture_args(Sender&& s)
{
  return {std::forward<Sender>(s)};
}
