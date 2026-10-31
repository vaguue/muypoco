# Events {#events}

`#include "muypoco/events.h"`

A Node-style `EventEmitter`. Event names are string literals checked at compile time. Static subscriptions get wired before `main`.

## Bus

```cpp
struct net : muypoco::bus<net> {};
inline constexpr muypoco::handle<net> net{};
```

The struct and the constant share one name on purpose: `net::` is the compile-time side, `net.` the runtime side.
Each bus has its own subscribers. `muypoco::events` is a ready-made default bus.

## Subscribe

| call | subscribed | handler |
|---|---|---|
| `net::on<"data">(f)` | before `main`, if the line is compiled | captureless lambda, empty functor |
| `net::on<"data", &fn>()` | before `main`, if the line is compiled | free function |
| `net::once<"data">(f)` | same as `net::on`, fires once | captureless lambda, empty functor |
| `net.on<"data">(f)` | every time the line runs | anything |
| `net.once<"data">(f)` | when the line runs, fires once | anything |
| `co_await net.once<"data", Data>()` | when awaited, fires once | coroutine |

```cpp
void wire() {
  net::on<"data">([](const Data& d) noexcept { /* ... */ });
}
```

`wire()` never has to be called. Putting `net::on` inside an `if` or a loop changes nothing: it still subscribes once.

Handlers must be `noexcept`, or the code doesn't compile. The payload type comes from the handler's parameter. Generic lambdas have to name it: `net::on<"data", Data>(...)`.

## Emit

```cpp
net.emit<"data">(Data{42});
```

Emit runs synchronously in the calling thread. Static subscribers run first, then runtime ones in the order they subscribed. The payload is passed by `const&` with no copies. Emit doesn't allocate, lock, throw, hash, or use RTTI. An event nobody listens to is a no-op.

Emitting from several threads is fine, and so is subscribing while another thread emits. A listener added during an emit runs from the next emit on.

## Examples

A bus per subsystem, so names don't clash:

```cpp
namespace net {
struct events : muypoco::bus<events> {};
inline constexpr muypoco::handle<events> events{};
}

net::events::on<"closed">([](const Connection& c) noexcept { release(c); });
net::events.emit<"closed">(connection);
```

A free function as a handler:

```cpp
void audit(const Request& r) noexcept;

void wiring() {
  muypoco::events::on<"request", &audit>();
}
```

Runtime state, owned by whoever subscribes:

```cpp
Stats& start_stats() {
  static Stats stats;
  muypoco::events.on<"request">([&stats](const Request& r) noexcept {
    stats.requests++;
    stats.bytes += r.bytes;
  });
  return stats;
}
```

Once:

```cpp
muypoco::events::once<"ready">([](const Config&) noexcept { warm_caches(); });
muypoco::events.once<"ready">([&](const Config& c) noexcept { port = c.port; });
```

Startup sequencing with coroutines:

```cpp
task boot() {
  const Config config = co_await muypoco::events.once<"config.loaded", Config>();
  const Database db = co_await muypoco::events.once<"db.ready", Database>();
  listen(config.port, db);
}
```

A generic lambda names the payload:

```cpp
muypoco::events::on<"request", Request>([](const auto& r) noexcept { trace(r.id); });
```

Moving work off the emitting thread with a Chan:

```cpp
muypoco::Chan<Reporter, Report, 64> reporter;
reporter.start();

app.on<"tick">([&reporter](const Tick& t) noexcept {
  reporter.tryEnqueue(Report{t.number});
});
```

Isolated buses in tests:

```cpp
void test_retry() {
  struct bus : muypoco::bus<bus> {};
  constexpr muypoco::handle<bus> bus{};
  int seen = 0;

  bus.on<"retry">([&seen](const Attempt&) noexcept { seen++; });
  bus.emit<"retry">(Attempt{1});
  assert(seen == 1);
}
```

Runnable versions are in [examples/](https://github.com/vaguue/muypoco/tree/main/examples).

## Backends

| backend | how | emit |
|---|---|---|
| registrar (default) | standard C++; each static subscriber is an inline variable whose initializer registers it | loop of indirect calls |
| inline, `-DMUYPOCO_EVENTS_INLINE` | whole program in one translation unit; friend injection, clang only | direct, inlinable calls |

4 handlers, ReleaseFast, Apple M-series: inline 1.5 ns, registrar 8.5 ns, runtime 9.8 ns, no subscribers 0.3 ns.

## Linking (registrar) {#linking}

A file that only contains `net::on` calls is referenced by nothing. If it sits in a static library, the linker drops it along with its subscribers. Link such modules as object files or with whole-archive:

```
-Wl,--whole-archive libservice.a -Wl,--no-whole-archive   # GNU ld, lld
-Wl,-force_load,libservice.a                              # Apple ld
target_link_libraries(app PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,service>")   # CMake
```

Zig 0.16 has no whole-archive switch, so build such modules with `b.addObject` and link them with `exe.root_module.addObject(object)`.

## Debug checks

Without `NDEBUG`, the first emit checks the whole program and aborts with a message if:

- something subscribes to an event nobody emits (typo);
- one event is used with two payload types;
- a registration happens after the first emit (something emitted before `main`);
- the inline backend is split across translation units.

Call `auto [ok, message] = muypoco::validate();` to run these checks yourself.

## Rules

- No unsubscribe.
- Runtime handlers are copied and kept forever. Whatever they capture by reference must outlive every emit.
- `co_await once` adds an entry per await. Don't use it in a hot loop.
