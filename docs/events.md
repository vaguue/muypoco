# Events {#events}

`#include "muypoco/events.h"`

A Node-style `EventEmitter`. Event names are string literals checked at compile time. Static subscriptions get wired before `main`.

It costs next to nothing:

- **`muypoco::events::on` in a single-file build:** `emit` compiles to the same instructions as calling the handlers by hand. No loop, no function pointers, nothing left of the emitter in the binary.
- **`muypoco::events::on` in a normal build, and `muypoco::events.on`:** one indirect call per handler, about 2 ns each. That's on par with, or a bit faster than, looping over a `std::vector<std::function>`.
- **No subscribers:** about 0.3 ns, one load and one branch.
- **Every emit:** no allocations, no locks, no strings, no hashing, no RTTI, no exceptions.

## Bus

```cpp
MUYPOCO_BUS(net);
```

That's a bus called `net`: `net::on` subscribes by presence of code, and `net.on` / `net.emit` work at runtime. Each bus has its own subscribers, so modules can each have their own without clashing. `muypoco::events` is a ready-made default bus.

Use it at namespace scope. It expands to a struct and a constant with the same name:

```cpp
struct net : muypoco::bus<net> {};
inline constexpr muypoco::handle<net> net{};
```

Inside a function, for example in a test, write those two lines yourself, with `constexpr` instead of `inline constexpr`.

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

A listener added during an emit runs from the next emit on.

## Threads {#threads}

- **`emit`** can be called from any thread, at the same time. It reads the subscriber list without locking.
- **`.on`** takes a short lock and is safe while other threads emit.
- **Handlers run on the thread that called `emit`,** synchronously. Nothing is delivered to another thread: a handler subscribed in `main` runs on the timer thread when a timer emits. If several threads emit the same event, the handler runs on all of them, possibly at once, so its state has to be thread-safe.
- **`once`** fires exactly once, even with concurrent emits.

To move work to another thread, enqueue it into a `Chan` from the handler:

```cpp
muypoco::Chan<Reporter, Report, 64> reporter;
reporter.start();

app.on<"tick">([&reporter](const Tick& tick) noexcept {
  reporter.enqueue(Report{tick.number});          // runs on the timer thread
});

muypoco::setInterval([]() noexcept {
  app.emit<"tick">(Tick{1});                      // timer thread
}, 5ms);
```

The timer thread emits, the handler runs right there, and `Reporter::process` runs on the Chan's worker thread. A Chan has exactly one producer, so if more than one thread emits that event, give each one its own Chan. Debug builds abort on a second producer. The full version is in [examples/pipeline.cpp](https://github.com/vaguue/muypoco/blob/main/examples/pipeline.cpp).

To handle an event on the main thread, forward it into a Chan whose loop runs on main. Call `run()` instead of `start()`:

```cpp
struct OnMain {
  void process(Tick& tick) noexcept;              // main thread
};

muypoco::Chan<OnMain, Tick, 64> mainLoop;

app::on<"tick">([](const Tick& tick) noexcept {
  mainLoop.enqueue(tick);                         // emitting thread
});

int main() {
  // ...
  mainLoop.run();                                 // until mainLoop.stop()
}
```

If main already has its own loop, call `mainLoop.drain()` once per iteration instead. It handles what's queued and returns without blocking.

Full version: [examples/main_thread.cpp](https://github.com/vaguue/muypoco/blob/main/examples/main_thread.cpp).

## Examples

A bus per subsystem, so names don't clash:

```cpp
namespace net {
MUYPOCO_BUS(events);
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

## Cost at runtime

`muypoco::events::on` has two implementations, picked at build time:

- **Normal build:** any number of `.cpp` files. Each `muypoco::events::on` adds a function pointer to the event's list before `main`, and `emit` calls them in a loop.
- **Single-file build (`-DMUYPOCO_EVENTS_INLINE`):** the whole program is one translation unit, so the compiler sees every `muypoco::events::on`, and `emit` becomes direct calls, the same code you'd write by hand. clang only.

`muypoco::events.on` always uses the list. It stores a function pointer plus a pointer to what the lambda captured.

`zig build bench`: one `emit` reaching 4 handlers, each doing a volatile write. ReleaseFast, Apple M-series.

| | ns per emit |
|---|---|
| calling the 4 handlers by hand | 2.9 |
| `muypoco::events::on`, single-file build | 1.4–1.9 |
| `muypoco::events::on`, normal build | 8.4–8.8 |
| `muypoco::events.on` | 9.8 |
| `std::vector<std::function>` loop | 9.9 |
| `muypoco::events.emit` with no subscribers | 0.3 |

In the single-file build, `emit` and the hand-written calls compile to the same instructions. The gap between them is the CPU, not the emitter.

## Linking (normal build) {#linking}

A file that only contains `net::on` calls is referenced by nothing. If it sits in a static library, the linker drops it along with its subscribers. Link such modules as object files or with whole-archive:

```
-Wl,--whole-archive libservice.a -Wl,--no-whole-archive   # GNU ld, lld
-Wl,-force_load,libservice.a                              # Apple ld
target_link_libraries(app PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,service>")   # CMake
```

Zig 0.16 has no whole-archive switch, so build such modules with `b.addObject` and link them with `exe.root_module.addObject(object)`.

## Libraries {#libraries}

A library that uses muypoco events, say dicker, doesn't pick the build mode. The app that uses it does. dicker's code (`::on`, `emit`) stays the same either way.

dicker ships one entry file that `#include`s all of its sources:

```cpp
// dicker.cpp
#include "src/core.cpp"
#include "src/audit.cpp"
```

The app uses it in one of two ways:

- **Normal build:** compile `dicker.cpp` as one more source file of the app. dicker's subscriptions live in the same object file as the rest of dicker, so the linker can't drop them, and no whole-archive is needed.
- **Single-file build:** the app's one `.cpp`, compiled with `-DMUYPOCO_EVENTS_INLINE`, includes it:

```cpp
// unity.cpp
#include "dicker.cpp"
#include "src/main.cpp"
```

In `build.zig`:

```zig
const dicker = b.dependency("dicker", .{});

// normal build
module.addCSourceFile(.{ .file = dicker.path("dicker.cpp"), .flags = &flags });

// single-file build
const unity = b.addWriteFiles().add("unity.cpp", "#include \"dicker.cpp\"\n#include \"src/main.cpp\"\n");
module.addIncludePath(dicker.path("."));
module.addIncludePath(b.path("."));
module.addCMacro("MUYPOCO_EVENTS_INLINE", "1");
module.addCSourceFile(.{ .file = unity, .flags = &flags });
```

In the single-file build, dicker's sources end up in one file with the app's and get compiled with the app's flags. So dicker's sources have to follow a few rules:

- no file-level `static` or anonymous-namespace names that could clash with someone else's;
- no `using namespace` at file scope;
- every `#define` gets an `#undef` at the end of its file;
- defines dicker needs go into `dicker.cpp` itself, not into its build flags;
- only code that uses muypoco events goes into the entry file. Third-party sources are compiled on their own.

Mixing modes, like dicker compiled normally and the app single-file, aborts before `main` with `muypoco: mixed builds: main.cpp uses the single-file build but dicker.cpp uses the normal build`. A working example is in [tests/library](https://github.com/vaguue/muypoco/tree/main/tests/library).

## Debug checks

Without `NDEBUG`, the first emit checks the whole program and aborts with a message if:

- something subscribes to an event nobody emits (typo);
- one event is used with two payload types;
- a registration happens after the first emit (something emitted before `main`);
- in a single-file build, a `::on` inside a template was instantiated after the `emit` it belongs to.

Call `auto [ok, message] = muypoco::validate();` to run these checks yourself.

Two more checks run in every build, release included:

- **Mixed builds** abort before `main`. Each file that includes `events.h` registers its build mode at startup, so this costs one comparison per file and nothing on `emit`.
- **Two single-file translation units** in one program fail to link, with `duplicate symbol muypoco_events_single_file_build_allows_one_translation_unit`.

## Rules

- No unsubscribe.
- Runtime handlers are copied and kept forever. Whatever they capture by reference must outlive every emit.
- `co_await once` adds an entry per await. Don't use it in a hot loop.
