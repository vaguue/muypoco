#pragma once

#include <atomic>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>

#include "storage.h"
#include "validation.h"

namespace muypoco::detail {

template <class Key>
struct next_event {
  using Payload = typename Key::payload;

  static_assert(std::is_copy_constructible_v<Payload>,
                "muypoco: co_await bus.once<\"event\", Payload>() copies the payload out of emit, so it must be copyable");

  struct ticket {
    std::atomic<bool> fired{false};
    next_event* waiter = nullptr;
  };

  std::optional<Payload> payload;
  std::coroutine_handle<> continuation;

  static void deliver(void* context, const Payload& payload) noexcept {
    ticket* pending = static_cast<ticket*>(context);

    if (pending->fired.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    pending->waiter->payload.emplace(payload);
    pending->waiter->continuation.resume();
  }

  bool await_ready() const noexcept {
    return false;
  }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    record_subscription<Key>();
    this->continuation = handle;
    storage<Key>.append({&next_event::deliver, new ticket{.waiter = this}});
  }

  Payload await_resume() noexcept {
    return std::move(*this->payload);
  }
};

}
