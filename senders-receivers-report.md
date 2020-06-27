---
title: Executors review -- Senders and Receivers
document: PXXXXR0
date: today
audience:
  - Library Evolution Working Group
author:
  - name: Gašper Ažman
    email: <gasper.azman@gmail.com>
  - name: Tony van Eerd
    email: tvaneerd@gmail.com
  - name: Thomas Rodgers
    email: rodgert@twrodgers.com
  - name: Tomasz Kamiński
    email: tomaszkam@gmail.com
  - name: Corentin Jabot
    email: corentin.jabot@gmail.com
  - name: Robert Leahy
    email: rleahy@rleahy.ca
  - name: Gordon Brown (author)
    email: gordon@codeplay.com
  - name: Kirk Shoop (author)
    email: kirkshoop@fb.com
  - name: Eric Niebler
    email: eniebler@fb.com
  - name: Dietmar Kühl
    email: dkuhl@bloomberg.com
---

# Abstract

This is the report of the Executors review group 2: Senders/Receivers.

# Findings

## Receivers that match any scheduler are impossible to write

The `scheduler` concept is underconstrained, because it does not define a sensible constraint for the produced sender. Therefore, one cannot write a receiver that would match any scheduler.

Specifically, `scheduler` does not define what kind of sender `execution::schedule` returns, as what will be propagated to `set_value` of the receiver is undefined. From my understanding, the produced sender should be a `void`-sender (call `set_value()`).

Is this an omission in requirements, or are senders produced from `schedule` all allowed to produce values? If the latter is true, how should algorithms like `via`, `on` handle these values? 

This is <https://github.com/executors/executors/issues/467> and <https://github.com/atomgalaxy/review-executor-sendrecv/issues/9> .

To illustarte, it is unclear what types be used as `Args...` and `E` for the below receiver, for `execution::submit(execution::schedule(sch), my_receiver)` to be valid for any `scheduler sch`:
```cpp
struct my_receiver
{
   void set_value(Args...);
   void set_error(E);
   void set_done();
};
```

## More examples needed in order to explain misunderstandings

We ran into trouble understanding the current intention for usage patterns of sender/receiver when trying to implement a telnet client. Kirk Shoop was kind enough to elaborate, but we never finished it regardless (although that was partly due to being dragged into other projects)..

The standard might not be a teaching document, but the papers should be. Kirk graciously supplied the following example of a telnetish-client:

```cpp
char const    hello[] = { 'h', 'e', 'l', 'l', 'o', '\n' };
char          buffer[1024];
io_context    context;

sync_wait(
   just(endpoint)                               // should really be resolve
     | async_connect(stream_socket(context))    // uses the result of the previous step 
     | async_send(hello)                        // uses the result of the previous step
     | async_receive(buffer)                    // uses the result of the previous step
     | tap([](auto&&){ std::cout << "received response\n";})
     | async_close(),                           // uses the result of the previous step
  context);
```

This snippet, obviously, just does something akin to a "ping" but it does illustrate how an asynchronous client could be implemented.  The [experimental implementation](https://github.com/dietmarkuehl/kuhllib/tree/sender-receiver/src/sender-receiver) of the [Networking TS](http://wg21.link/n4771) using sender/receivers got to the point to make the above code operational.  An example of a telnet-like client implementation, for instance, would be welcome.

The [experimental implementation](https://github.com/dietmarkuehl/kuhllib/tree/sender-receiver/src/sender-receiver) was done natively instead of using the facilities from the [Networking TS](http://wg21.link/n4771). In retrospect it seems possible to use the interfaces from the [Networking TS](http://wg21.link/n4771) to implement the asynchronous operations in terms of sender/receivers, though: the sender would still be a custom class producing an operation state object. However, the actual asynchronous operation could use the completion token abstraction to plug into the sender/receiver protocol using the operation state. Such an implementation of the asynchronous operations isn't done, yet.

It's also noted that the paper currently comes with quite a few TODOs, mainly about streams, which should probably be worked out before we ship the design. Without a streams concept it is unclear how to use sender/receivers to process repeated operations like repeatedly reading packets from a socket or repeatedly accepting a client connection when listening on a port.

Main discussion: https://github.com/atomgalaxy/review-executor-sendrecv/issues/11

## The `typed_sender` :: has-error-types definition has wrong template parameter kind

The `typed_sender` definition uses the following `S::error_types` detection template:

```cpp
template<template<class...> class Variant>
struct has-error-types; // exposition only
```

However, the `S::error_types` is a template that accepts a single `Variant` parameter, so it should be:

```cpp
template<template<template<class...> class Variant> class>
struct has-error-types; // exposition only
```

This is issue <https://github.com/atomgalaxy/review-executor-sendrecv/issues/10>.

## `as_receiver::set_value` should preserve the value category of the invocable

Given that:

- `set_value` (when it succeeds) is "terminal" and
- `as-receiver::set_error` and `::set_done` don't make use of `as-receiver::f_`

`as-receiver::set_value` could be changed from:

```cpp
void set_value() noexcept(is_nothrow_invocable_v<F&>) {
  invoke(f_);
}
```

to

```cpp
void set_value() noexcept(is_nothrow_invocable_v<F&&>) {
  invoke(move(f_));
}
```

The executors team noted that this was because of the way in which `executor_of`
is specified (it only requires that the provided function object type be lvalue
invocable). This could be an argument for changing both `as-receiver::set_value`
and `executor_of`.

If we examine `executor_of` with respect to `inline_executor` (which is in P0443
as an example) we find that:

- `inline_executor::execute` is sensible in that it propagates the value
  category of the function object when invoking it
- For some type `F` which is lvalue invocable but not rvalue invocable (and
  which is also default constructible for ease of exposition):
  - `executor_of<inline_executor, F>` is satistied but
  - `inline_executor{}.execute(F{})` is ill-formed

This is issue <https://github.com/atomgalaxy/review-executor-sendrecv/issues/8>.

## Clarify when senders are reusable

The concepts seem to suggest that checking whether a sender is reusable is done using the concepts

- `sender_to<S, R>` or equivalent `sender_to<S&&, R&&>` for once-sender
- `sender_to<S&, R>` for multi-sender

but the paper doesn't seem to provide rationale for this.

TODO (Robert): please verify and expand this section to summarize issue.

This is issue <https://github.com/atomgalaxy/review-executor-sendrecv/issues/7>.

## Flesh out the discussion on the ref-qualification of `as-invocable::operator()`

The motivation for the function call operator of `as-invocable` being mutable lvalue ref qualified is unclear.

It doesn't make sense conceptually, given that the "receiver contract"
(§1.5.1) implies that invoking the function call operator of an instance of
`as-invocable` is "terminal" (since either `std::execution::set_value` will
be called and not throw, or `::set_error` will be called). As such, this
function being invocable on rvalues only seems perfectly legitimate. If
anything it seems to me that it should be disallowed to call it on lvalues,
not vice versa.

Beyond not making conceptual sense §2.2.3.4 doesn't seem to prohibit
`std::execution::execute` from treating submitted function objects as rvalues
when invoking them. In fact the example `inline_executor` (§1.3) potentially
does exactly this (as it employs perfect forwarding).

Which means that `inline_executor` [doesn't seem to work](https://godbolt.org/z/zP8hUA) with the current formulation of `as-invocable`.

Its implementation also moves from the pointed-to object `r_`, which further makes this pretty weird.

TODO (Robert): check this section

The paper should also mention the exception safety of first moving from `r_`
in `execution::set_value(std::move(*r_));` and then calling
`execution::set_error(std::move(*r_), current_exception());` on an exception.
The executors team noted that this was discussed, but the paper lacks clarity
on this decision.

More details in <https://github.com/atomgalaxy/review-executor-sendrecv/issues/5>.

## The fallback wrapping providing implicit convertibility between types satisfying disparate concepts is confusinghttps://github.com/atomgalaxy/review-executor-sendrecv/issues/4

The current definitions of customization points are conflating the concepts,
by providing a fallback wrapping. For example:

1. `executor` is `scheduler`:<br>
    The `exeuction::schedule` has a fallback defintion for the `executor`, that wraps int into void-sender. That means that for every `executor e` the `execution::schedule(e)` is well-formed, as consequence every `executor` is `scheduler` (both are `copy_constructible` and `equality_comparable`), but the `executor` does not subsume `scheduler`.

2. `executor` is `void-sender`, `void-sender` is `executor`:<br>
    The fallbacks in `connect` automatically wraps executor in sender, but
    again executor does not subsume `void_sender`.
    Also `execution::execute` works with any `void_sender`, so
    `copy_constructible` and `equality_comparable` sender is executor.

This makes it difficult to understand the ergonomics of the design.

The `sender_traits<S>` have as specialization for `void_sender` that is
enabled if `executor-of-impl<S,as-invocable<void-receiver, S>>`, which
imposes `copy_constructible` and `equality_comparable` requirements, instead
of `sender_to<S, void-receiver>`. This means that this specialization is
enabled for `copy_constructible` senders (because they are `executors`), but
not for normal senders.

The problem is not the ability to adapt the `executor` to `void-sender`, but
that this adaptation is implicit, this making executors model both `sender`
and `scheduler` implicitly. The paper does not discuss the reason for the
implicit wrapping.

We encourage exploring a design where `executor` implicitly models
`scheduler`, and `schedule(e)` is a syntax transforming an `executor` into a
`sender`, without a special case in `execution::connect`. The current
automatic wrapping is inefficient due to being partial, for example
`execution::submit(e, r)` falls back to the default cause that uses
`submit-receiver` and causes allocation, while submit can be implemented more
efficiently for `executor` as:

```cpp
template<typename Receiver>
void executor_sender::submit(Receiver&& r)
{
    execution::execute(e, [r = std::forward<R>(r)] {
         try {
            execution::set_value(r);
        } catch {
            execution::set_error(r, std::current_exception());
        }
    });
}
```

(Because receivers are movable).

The paper also does not explain why `sender` can implicitly be treated as an
`executor` via `execution::execute`. The problem with that wrapping is that
it chooses one error handling strategy (terminate) when others may be
possible, e.g. if a passed `invocable` has an `execption_ptr` overload, or
receives an `error_code` argument. We encourage the authors to explore
removing this implicit adaptation and eventually provide `as_receiver` as a
library utility.

The authors of 0443 have noted this is a sensible direction.

This is issue <https://github.com/atomgalaxy/review-executor-sendrecv/issues/4>

## Simplify types in concept definitions

This one is editorial.

In multiple places, the document refers to types using a phrase like the one below (this specific one is from 2.2.3.4):

    ... let `E` be a type such that `decltype((e))` is `E` ...

This should be simplified to:

    ... let `E` be `decltype((e))` ...

Casey Carter has acknowledged this and it seems to make sense.

## Implementation of critical section-capable scheduling, a need to extract scheduler from context

This is issue <https://github.com/atomgalaxy/review-executor-sendrecv/issues/1>.

Tomasz implemented a scheduler and support for critical-section capable sender, receiver, and scheduler, along with an `async_mutex`.

The major requirement for the projects were:

 * avoid dynamic allocation
 * provide RAII semantics (mutex is automatically locked/unlocked)
 * preserve the execution context of the work

The final requirement is not expressible with the current design ([P0443](wg21.link/p0443)), as 
to resume an "blocked" work, one needs to have the ability to extract the scheduler from the current execution context.

Initially, Tomasz used his own `sender_with_scheduler` concept to provide this functionality, as 
`schedule_provider`, as proposed in [P1898](wg21.link/p1898), seemed unsuitable - it was impossible to continue on the
same executor where `set_value` was invoked, and not the one provided by the associated `receiver`.

After a discussion with the authors in the [issue](https://github.com/atomgalaxy/review-executor-sendrecv/issues/12),
the group found a way to use the proposed `schedule_provider` for this particular case. However, the semantic (forward/backward) for the propagation of the scheduler is still unclear.

We suggest another example be provided to avoid further misunderstandings regarding the intended usage.

## Forward/Backward propagation of executors

This is related to the extension of the senders/receiver design, proposed in  [P1898](wg21.link/p1898). 
However, this touches a very basic semantics of the algorithms defined in terms of sender/receiver, and 
we think that it would be beneficial to clarfiy this up-front.

With the `schedule_provider` concept it is possible to have two competing `schedulers` for the 
single operation, and it is unclear where it will be actually executed.

Specifically:

```cpp
auto s1 = on(std::move(previous), sch1);
auto s2 = work(std::move(s1));
auto s3 = on(std::move(s2), sch2);
```
Does `work` run on the `sch1` propagated from `s1` or use `s2` provided by `schedule_provider` `on(..., sch2)`, and why?

Another example:

```cpp
on(just(val), sch);
```

Does `just` extract the scheduler from the receiver of `on` and run there, or does `just` execute inline?

