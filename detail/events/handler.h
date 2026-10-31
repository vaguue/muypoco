#pragma once

#include <atomic>
#include <type_traits>
#include <utility>

namespace muypoco::detail {

template <class Handler>
concept stateless = std::is_empty_v<Handler> && std::is_default_constructible_v<Handler>;

struct undeducible_payload {};

template <class Signature>
struct parameter_of {
  using type = undeducible_payload;
};

template <class Result, class Argument>
struct parameter_of<Result(Argument)> {
  using type = Argument;
};

template <class Result, class Argument>
struct parameter_of<Result(Argument) noexcept> {
  using type = Argument;
};

template <class Result, class Owner, class Argument>
struct parameter_of<Result (Owner::*)(Argument)> {
  using type = Argument;
};

template <class Result, class Owner, class Argument>
struct parameter_of<Result (Owner::*)(Argument) const> {
  using type = Argument;
};

template <class Result, class Owner, class Argument>
struct parameter_of<Result (Owner::*)(Argument) noexcept> {
  using type = Argument;
};

template <class Result, class Owner, class Argument>
struct parameter_of<Result (Owner::*)(Argument) const noexcept> {
  using type = Argument;
};

template <class Handler>
consteval auto deduce_payload() {
  if constexpr (std::is_pointer_v<Handler> && std::is_function_v<std::remove_pointer_t<Handler>>) {
    return std::type_identity<typename parameter_of<std::remove_pointer_t<Handler>>::type>{};
  }
  else if constexpr (requires { &Handler::operator(); }) {
    return std::type_identity<typename parameter_of<decltype(&Handler::operator())>::type>{};
  }
  else {
    return std::type_identity<undeducible_payload>{};
  }
}

template <class Payload, class Handler>
using payload_t = std::remove_cvref_t<typename std::conditional_t<
  std::is_void_v<Payload>,
  decltype(deduce_payload<Handler>()),
  std::type_identity<Payload>
>::type>;

template <class Key, class Handler>
consteval bool check_handler() {
  using Payload = typename Key::payload;

  if constexpr (std::is_same_v<Payload, undeducible_payload>) {
    static_assert(!std::is_same_v<Payload, undeducible_payload>,
                  "muypoco: cannot deduce the payload type from this handler (generic lambda or overloaded functor?); "
                  "name it explicitly: on<\"event\", Payload>(handler)");
  }
  else {
    static_assert(std::is_nothrow_invocable_v<Handler&, const Payload&>,
                  "muypoco: event handlers must be noexcept and take the payload by const reference or by value, "
                  "e.g. [](const Payload& payload) noexcept { ... }");
  }
  return true;
}

template <class Key, class Handler>
consteval bool check_static_handler() {
  static_assert(stateless<Handler>,
                "muypoco: Bus::on subscribes by presence of code and needs a stateless handler "
                "(captureless lambda, empty functor or free function); subscribe capturing handlers with bus.on");
  return check_handler<Key, Handler>();
}

template <class Payload, class Handler>
void call_stateless(void*, const Payload& payload) noexcept {
  Handler{}(payload);
}

template <class Payload, class Handler>
void call_stateful(void* context, const Payload& payload) noexcept {
  (*static_cast<Handler*>(context))(payload);
}

template <auto Function>
struct function_handler {
  template <class Payload>
  void operator()(const Payload& payload) const noexcept(noexcept(Function(payload))) {
    Function(payload);
  }
};

template <class Key, class Handler>
struct fire_once {
  static inline std::atomic<bool> fired{false};

  void operator()(const typename Key::payload& payload) const noexcept {
    if (fired.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    Handler{}(payload);
  }
};

template <class Handler>
struct runtime_once {
  std::atomic<bool> fired{false};
  Handler handler;

  explicit runtime_once(Handler&& source) : handler(std::move(source)) {
  }

  explicit runtime_once(const Handler& source) : handler(source) {
  }

  template <class Payload>
  void operator()(const Payload& payload) noexcept {
    if (this->fired.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    this->handler(payload);
  }
};

}
