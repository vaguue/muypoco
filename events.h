#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "detail/events/fixed_string.h"
#include "detail/events/key.h"
#include "detail/events/handler.h"
#include "detail/events/storage.h"
#include "detail/events/validation.h"
#include "detail/events/backend.h"
#include "detail/events/subscribe.h"
#include "detail/events/awaitable.h"

/// @file events.h
/// Node.js-style event emitter with subscriptions wired at compile time.

namespace muypoco {

/// Compile-time half of a bus. Declare a bus as a struct deriving from it,
/// plus a muypoco::handle constant with the same name:
/// @code
/// struct net : muypoco::bus<net> {};
/// inline constexpr muypoco::handle<net> net{};
/// @endcode
/// `net::` subscribes by presence of code, `net.` subscribes at runtime.
template <class Bus>
struct bus {
  /// Static subscription. It exists because this line is compiled, not because it runs:
  /// registered before `main`, once, even inside an `if` or a loop.
  /// The handler must be a captureless `noexcept` lambda or an empty functor.
  /// The payload type is deduced from its parameter; name it for generic lambdas: `on<"data", Data>(...)`.
  template <
    fixed_string Name,
    class Payload = void,
    class Handler,
    class Key = detail::key<Bus, Name, detail::payload_t<Payload, Handler>>,
    std::size_t = detail::declare<Key, Handler, Handler>()
  >
  static void on(Handler) noexcept {
    detail::anchor<Key, Handler>();
  }

  /// Static subscription of a free function: `net::on<"data", &handler>()`.
  template <
    fixed_string Name,
    auto Function,
    class Key = detail::key<Bus, Name, detail::payload_t<void, decltype(Function)>>,
    std::size_t = detail::declare<Key, detail::function_handler<Function>, detail::function_handler<Function>>()
  >
  static void on() noexcept {
    detail::anchor<Key, detail::function_handler<Function>>();
  }

  /// Static subscription that fires on the first emit only, once per program run.
  template <
    fixed_string Name,
    class Payload = void,
    class Handler,
    class Key = detail::key<Bus, Name, detail::payload_t<Payload, Handler>>,
    std::size_t = detail::declare<Key, Handler, detail::fire_once<Key, Handler>>()
  >
  static void once(Handler) noexcept {
    detail::anchor<Key, detail::fire_once<Key, Handler>>();
  }

  /// Calls every subscriber of `Name` with this payload type, synchronously, in the calling thread:
  /// static subscribers first, then runtime ones in subscription order.
  /// No allocations, locks or exceptions. No subscribers is a no-op.
  template <fixed_string Name, class Payload>
  static void emit(const Payload& payload) noexcept {
    using Key = detail::key<Bus, Name, std::remove_cvref_t<Payload>>;

    detail::record_emission<Key>();
    detail::validate_or_abort();
    detail::dispatch<Key>(payload);
  }
};

/// Runtime half of a bus, see muypoco::bus.
template <class Bus>
struct handle {
  /// The bus type, for when the shared name hides it.
  using type = Bus;

  /// Runtime subscription: subscribes every time it runs, like Node's `on`.
  /// The handler is copied and kept for the life of the program; whatever it captures
  /// by reference must outlive every emit. Allocates only for capturing handlers.
  template <fixed_string Name, class Payload = void, class Handler>
  void on(Handler&& handler) const noexcept {
    using Stored = std::decay_t<Handler>;
    using Key = detail::key<Bus, Name, detail::payload_t<Payload, Stored>>;

    detail::subscribe<Key, Stored>(std::forward<Handler>(handler));
  }

  /// Runtime subscription that fires on the next emit only.
  template <fixed_string Name, class Payload = void, class Handler>
  void once(Handler&& handler) const noexcept {
    using Stored = std::decay_t<Handler>;
    using Key = detail::key<Bus, Name, detail::payload_t<Payload, Stored>>;

    detail::subscribe<Key, detail::runtime_once<Stored>>(std::forward<Handler>(handler));
  }

  /// Awaitable once: `Data data = co_await net.once<"data", Data>();`
  /// Resumes inside the next emit, on the emitting thread, with a copy of the payload.
  /// Each await leaves a dead entry behind, so don't await it in a hot loop.
  template <fixed_string Name, class Payload>
  detail::next_event<detail::key<Bus, Name, Payload>> once() const noexcept {
    return {};
  }

  /// Same as muypoco::bus::emit.
  template <fixed_string Name, class Payload>
  void emit(const Payload& payload) const noexcept {
    Bus::template emit<Name>(payload);
  }
};

/// Runs the debug checks now instead of on the first emit.
/// Reports a subscription to an event nobody emits and an event used with two payload types.
/// Always ok when `NDEBUG` is defined.
inline validation validate() noexcept {
  return detail::validate_once();
}

/// Default bus: `muypoco::events::on<"data">(...)`, `muypoco::events.emit<"data">(...)`.
struct events : bus<events> {};

/// Runtime half of the default bus.
inline constexpr handle<events> events{};

}
