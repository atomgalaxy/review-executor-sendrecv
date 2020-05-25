#ifndef GODBOLT_COMPATIBLE
#pragma once
#endif // GODBOLT_COMPATIBLE

#include <functional>
#include <type_traits>
#include <concepts>

template<typename R>
concept receiver = true;

template<typename R, typename... Args>
concept receiver_of = receiver<R> && true;

template<typename S>
concept sender = true;

template<template<template<class...> class Tuple, template<class...> class Variant> class>
struct has_value_types; // exposition only

template<template<template<class...> class Variant> class>
struct has_error_types; // exposition only

template<class S>
concept has_sender_types = // exposition only
 requires {
   typename has_value_types<S::template value_types>;
   typename has_error_types<S::template error_types>;
   typename std::bool_constant<S::sends_done>;
 };

template<class S>
concept typed_sender =
  sender<S> &&
  has_sender_types<std::remove_cvref_t<S>>;

template<typename S, typename R>
concept sender_to = sender<R> && receiver<R> && true;

template<typename S>
concept scheduler = true;

template<typename S>
concept sender_with_scheduler = sender<S> && 
  requires (std::remove_cvref_t<S> const& s) { 
    { s.scheduler() } -> scheduler;
  };

template<typename S>
struct sender_traits 
{
    template<template<class...> class Tuple, template<class...> class Variant>
    using value_types = typename S::template value_types<Tuple, Variant>;

    template<template<class...> class Variant>
    using error_types = typename S::template error_types<Variant>;

    static constexpr bool sends_done = S::sends_done;
};

template<sender S, receiver R>
using operation_state_type = decltype(connect(std::declval<S>(), std::declval<R>()));

template<std::invocable F>
    requires std::is_nothrow_move_constructible_v<F>
struct init_from_invoke {
    F f_;
    explicit init_from_invoke(F f) noexcept : f_((F&&) f) {}
    operator std::invoke_result_t<F>() && {
        return ((F&&) f_)();
    }
};
