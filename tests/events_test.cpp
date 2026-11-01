#include "events.h"

#include <coroutine>
#include <string>

#include "check.h"

using testing::check;

struct Data {
  int value;
};

struct pinned {
  int value;

  explicit pinned(int initial) : value(initial) {
  }

  pinned(const pinned&) = delete;
  pinned& operator=(const pinned&) = delete;
};

struct order_bus : muypoco::bus<order_bus> {};
inline constexpr muypoco::handle<order_bus> order_bus{};

int emitted_before_subscribed = 0;

void emit_before_subscribing() {
  order_bus.emit<"early">(Data{7});
}

void subscribe_after_emitting() {
  order_bus::on<"early">([](const Data& data) noexcept {
    emitted_before_subscribed += data.value;
  });
}

struct single_bus : muypoco::bus<single_bus> {};
inline constexpr muypoco::handle<single_bus> single_bus{};

int single_hits = 0;

void never_called_single() {
  single_bus::on<"data">([](const Data& data) noexcept {
    single_hits += data.value;
  });
}

struct multi_bus : muypoco::bus<multi_bus> {};
inline constexpr muypoco::handle<multi_bus> multi_bus{};

int multi_hits = 0;

void never_called_multi() {
  multi_bus::on<"data">([](const Data& data) noexcept {
    multi_hits += data.value;
  });
  multi_bus::on<"data">([](const Data& data) noexcept {
    multi_hits += data.value * 10;
  });
  multi_bus::on<"data">([](const Data& data) noexcept {
    multi_hits += data.value * 100;
  });
}

struct function_bus : muypoco::bus<function_bus> {};
inline constexpr muypoco::handle<function_bus> function_bus{};

int function_hits = 0;

void on_function_data(const Data& data) noexcept {
  function_hits += data.value;
}

void never_called_function() {
  function_bus::on<"data", &on_function_data>();
}

struct functor_bus : muypoco::bus<functor_bus> {};
inline constexpr muypoco::handle<functor_bus> functor_bus{};

int functor_hits = 0;

struct functor_handler {
  void operator()(const Data& data) const noexcept {
    functor_hits += data.value;
  }
};

void never_called_functor() {
  functor_bus::on<"data">(functor_handler{});
}

struct loop_bus : muypoco::bus<loop_bus> {};
inline constexpr muypoco::handle<loop_bus> loop_bus{};

int loop_hits = 0;

void subscribe_in_loop() {
  for (int i = 0; i < 3; i++) {
    loop_bus::on<"data">([](const Data& data) noexcept {
      loop_hits += data.value;
    });
  }
}

struct generic_bus : muypoco::bus<generic_bus> {};
inline constexpr muypoco::handle<generic_bus> generic_bus{};

int generic_hits = 0;

void never_called_generic() {
  generic_bus::on<"data", Data>([](const auto& data) noexcept {
    generic_hits += data.value;
  });
}

struct copy_bus : muypoco::bus<copy_bus> {};
inline constexpr muypoco::handle<copy_bus> copy_bus{};

const pinned* seen_static = nullptr;
const pinned* seen_runtime = nullptr;

void never_called_copy() {
  copy_bus::on<"pinned">([](const pinned& payload) noexcept {
    seen_static = &payload;
  });
}

struct sequence_bus : muypoco::bus<sequence_bus> {};
inline constexpr muypoco::handle<sequence_bus> sequence_bus{};

std::string sequence;

void never_called_sequence() {
  sequence_bus::on<"step">([](const Data&) noexcept {
    sequence += "s";
  });
}

struct once_bus : muypoco::bus<once_bus> {};
inline constexpr muypoco::handle<once_bus> once_bus{};

int static_once_hits = 0;

void never_called_once() {
  once_bus::once<"data">([](const Data& data) noexcept {
    static_once_hits += data.value;
  });
}

MUYPOCO_BUS(alpha);

namespace module {
MUYPOCO_BUS(beta);
}

using module::beta;

int alpha_hits = 0;
int beta_hits = 0;

void never_called_isolation() {
  alpha::on<"data">([](const Data& data) noexcept {
    alpha_hits += data.value;
  });
  beta::on<"data">([](const Data& data) noexcept {
    beta_hits += data.value;
  });
}

int default_hits = 0;

void never_called_default() {
  muypoco::events::on<"default.data">([](const Data& data) noexcept {
    default_hits += data.value;
  });
}

struct awaited_bus : muypoco::bus<awaited_bus> {};
inline constexpr muypoco::handle<awaited_bus> awaited_bus{};

struct detached {
  struct promise_type {
    detached get_return_object() noexcept {
      return {};
    }

    std::suspend_never initial_suspend() noexcept {
      return {};
    }

    std::suspend_never final_suspend() noexcept {
      return {};
    }

    void return_void() noexcept {
    }

    void unhandled_exception() noexcept {
    }
  };
};

int awaited_value = 0;
int awaited_resumes = 0;

detached wait_for_ready() {
  const Data data = co_await awaited_bus.once<"ready", Data>();
  awaited_value = data.value;
  awaited_resumes++;
}

void test_emit_before_subscribe() {
  emit_before_subscribing();
  check(emitted_before_subscribed == 7, "a subscription written after the emitting code in the TU still fires");
}

void test_single_subscriber() {
  single_bus.emit<"data">(Data{3});
  check(single_hits == 3, "single static subscriber fires though its function never ran");
}

void test_multiple_subscribers() {
  multi_bus::emit<"data">(Data{1});
  check(multi_hits == 111, "all static subscribers fire");
}

void test_free_function() {
  function_bus.emit<"data">(Data{4});
  check(function_hits == 4, "free function subscribed as NTTP fires");
}

void test_functor() {
  functor_bus.emit<"data">(Data{6});
  check(functor_hits == 6, "empty functor subscribed statically fires");
}

void test_static_subscription_is_by_presence() {
  subscribe_in_loop();
  loop_bus.emit<"data">(Data{1});
  check(loop_hits == 1, "Bus::on in a loop subscribes once");
}

void test_generic_lambda() {
  generic_bus.emit<"data">(Data{9});
  check(generic_hits == 9, "generic lambda with explicit payload fires");
}

void test_runtime_capturing() {
  struct runtime_bus : muypoco::bus<runtime_bus> {};
  constexpr muypoco::handle<runtime_bus> runtime_bus{};
  int total = 0;

  runtime_bus.emit<"data">(Data{100});
  runtime_bus.on<"data">([&total](const Data& data) noexcept {
    total += data.value;
  });
  runtime_bus.emit<"data">(Data{2});
  check(total == 2, "capturing handler subscribes when bus.on runs");
}

void test_runtime_stateless_subscribes_per_call() {
  struct twice_bus : muypoco::bus<twice_bus> {};
  constexpr muypoco::handle<twice_bus> twice_bus{};
  static int hits = 0;

  for (int i = 0; i < 2; i++) {
    twice_bus.on<"data">([](const Data& data) noexcept {
      hits += data.value;
    });
  }
  twice_bus.emit<"data">(Data{5});
  check(hits == 10, "bus.on with a captureless lambda subscribes on every call");
}

void test_mutable_runtime_handler() {
  struct mutable_bus : muypoco::bus<mutable_bus> {};
  constexpr muypoco::handle<mutable_bus> mutable_bus{};
  int last = 0;

  mutable_bus.on<"data">([calls = 0, &last](const Data&) mutable noexcept {
    calls++;
    last = calls;
  });
  mutable_bus.emit<"data">(Data{0});
  mutable_bus.emit<"data">(Data{0});
  check(last == 2, "mutable lambda keeps its state between emits");
}

void test_no_subscribers() {
  struct empty_bus : muypoco::bus<empty_bus> {};
  constexpr muypoco::handle<empty_bus> empty_bus{};

  empty_bus.emit<"nobody">(Data{1});
  check(true, "emit with no subscribers is a silent no-op");
}

void test_no_copies() {
  const pinned payload(42);

  copy_bus.on<"pinned">([](const pinned& received) noexcept {
    seen_runtime = &received;
  });
  copy_bus.emit<"pinned">(payload);
  check(seen_static == &payload, "static handler receives the emitted object itself");
  check(seen_runtime == &payload, "runtime handler receives the emitted object itself");
}

void test_order_static_then_runtime() {
  sequence_bus.on<"step">([](const Data&) noexcept {
    sequence += "1";
  });
  sequence_bus.on<"step">([](const Data&) noexcept {
    sequence += "2";
  });
  sequence_bus.emit<"step">(Data{0});
  check(sequence == "s12", "static subscribers run first, then runtime ones in subscription order");
}

void test_subscribe_during_emit() {
  struct nested_bus : muypoco::bus<nested_bus> {};
  static constexpr muypoco::handle<nested_bus> nested_bus{};
  static int inner_hits = 0;

  nested_bus.on<"data">([](const Data&) noexcept {
    nested_bus.on<"data">([](const Data&) noexcept {
      inner_hits++;
    });
  });
  nested_bus.emit<"data">(Data{0});
  check(inner_hits == 0, "a listener added during emit does not fire in that emit");
  nested_bus.emit<"data">(Data{0});
  check(inner_hits == 1, "a listener added during emit fires on the next emit");
}

void test_once() {
  int runtime_once_hits = 0;

  once_bus.once<"data">([&runtime_once_hits](const Data& data) noexcept {
    runtime_once_hits += data.value;
  });
  for (int i = 0; i < 3; i++) {
    once_bus.emit<"data">(Data{2});
  }
  check(static_once_hits == 2, "static once fires a single time");
  check(runtime_once_hits == 2, "runtime once fires a single time");
}

void test_awaitable_once() {
  wait_for_ready();
  check(awaited_resumes == 0, "coroutine stays suspended until the event");
  awaited_bus.emit<"ready">(Data{11});
  check(awaited_value == 11 && awaited_resumes == 1, "co_await bus.once resumes with the payload");
  awaited_bus.emit<"ready">(Data{12});
  check(awaited_resumes == 1, "awaited once does not resume twice");
}

void test_bus_isolation() {
  alpha.emit<"data">(Data{1});
  check(alpha_hits == 1 && beta_hits == 0, "same event name on another bus is isolated");
  beta::emit<"data">(Data{2});
  check(alpha_hits == 1 && beta_hits == 2, "Bus::emit and bus.emit reach the same subscribers");
}

void test_default_bus() {
  muypoco::events.emit<"default.data">(Data{8});
  check(default_hits == 8, "the default muypoco::events bus works");
}

void test_validate() {
  const auto [ok, message] = muypoco::validate();
  check(ok, message);
}

int main() {
  test_validate();
  test_emit_before_subscribe();
  test_single_subscriber();
  test_multiple_subscribers();
  test_free_function();
  test_functor();
  test_static_subscription_is_by_presence();
  test_generic_lambda();
  test_runtime_capturing();
  test_runtime_stateless_subscribes_per_call();
  test_mutable_runtime_handler();
  test_no_subscribers();
  test_no_copies();
  test_order_static_then_runtime();
  test_subscribe_during_emit();
  test_once();
  test_awaitable_once();
  test_bus_isolation();
  test_default_bus();
#ifdef MUYPOCO_EVENTS_INLINE
  return testing::finish("events (inline backend)");
#else
  return testing::finish("events (registrar backend)");
#endif
}
