#pragma once

#include <functional>
#include <type_traits>
#include <concepts>

template<typename R>
concept receiver = true;

template<typename R, typename... Args>
concept receiver_of = receiver<R> && true;

template<typename S>
concept sender = true;

template<typename S>
concept typed_sender = sender<S> && true;

template<typename S, typename R>
concept sender_to = sender<R> && receiver<R> && true;

template<typename S>
concept scheduler = true;

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
