# muypoco

Header-only C++20 bits I keep reusing. Not [POCO](https://pocoproject.org), just *muy poco*.

| header | |
|---|---|
| `events.h` | Node-style EventEmitter, subscriptions wired at compile time |
| `chan.h` | single-producer channel with its own worker thread |
| `timers.h` | `setTimeout`, `setInterval` (a thread per timer), stopwatch, throttle |
| `log.h` | printf-style logger, one write per line or batched in the background |

No exceptions: things return `ok`. Builds with `-fno-exceptions -fno-rtti`.

## Design

There are a few ideas from other ecosystems I like. Here I reimplement them as small building blocks and port them the C++ way, not as 1:1 copies.

**Events.** I like the observer pattern. But C++ is mostly used when performance matters, so this isn't Node's EventEmitter: nothing is deferred to a later tick, there's no unsubscribing, and `emit` is a plain synchronous call. It's code sugar that lets modules talk to each other without knowing about each other, so you can organize your code better.

**Chan.** Inspired by Go channels, but way more limited on purpose: one producer, one consumer, one worker thread, a fixed-size buffer. It's a simple, fast and reliable way to add threading to a codebase.

**Timers.** Every timer runs on its own thread. That's a choice. In C++ nobody writes `await new Promise(resolve => setTimeout(resolve, 3e3))` for small things. In my experience a program has fewer than 10 timer-driven jobs, each one a complete job of its own, and giving each its own thread makes more sense than loading the main thread.

It's all very opinionated, and I don't give a shit.

## Example

```cpp
#include <cstdio>

#include "muypoco/events.h"

struct Request {
  int id;
};

void audit() {
  muypoco::events::on<"request">([](const Request& r) noexcept {
    std::printf("request %d\n", r.id);
  });
}

int main() {
  int count = 0;

  muypoco::events.on<"request">([&](const Request&) noexcept {
    count++;
  });

  muypoco::events.emit<"request">(Request{1});
}
```

`audit()` is never called and still prints: `::on` subscribes because the code exists, `.on` subscribes when it runs.

Emit is cheap. In a single-file build, `emit` compiles to the same code as calling the handlers by hand. In a normal build it costs about 2 ns per handler, and an event nobody listens to costs 0.3 ns. [Numbers](https://vaguue.github.io/muypoco/events.html).

```cpp
muypoco::Chan<Store, Write, 1024> store;
store.start();
store.enqueue(Write{"session", "42"});

int id = muypoco::setInterval([]() noexcept { ping(); }, 1s);
muypoco::clearInterval(id);

muypoco::log().start();
muypoco::log().info("listening on %d", 8080);
```

More in [examples/](https://github.com/vaguue/muypoco/tree/main/examples) and the [docs](https://vaguue.github.io/muypoco).

## Use

You need C++20 and clang. It's tested with the clang 21 that ships with Zig 0.16 on macOS, and CI runs the tests on Linux and macOS.

### build.zig

```
zig fetch --save git+https://github.com/vaguue/muypoco
```

```zig
const muypoco = b.dependency("muypoco", .{});
exe.root_module.addIncludePath(muypoco.namedLazyPath("include"));
```

```cpp
#include "muypoco/events.h"
```

This needs Zig 0.16.0. For the single-file build, compile the program as one translation unit and add `exe.root_module.addCMacro("MUYPOCO_EVENTS_INLINE", "1");`.

In a normal build, a file that only contains `::on` subscriptions must be linked as an object (`exe.root_module.addObject(...)`), not through a static library, or the linker drops it. See [linking](https://vaguue.github.io/muypoco/events.html#linking).

#### cxx_modules

Zig 0.17 (still in development) can keep fetched packages in a project folder set by `ZIG_LOCAL_PKG_DIR`. Which finally makes this a real command:

```
ZIG_LOCAL_PKG_DIR=cxx_modules zig build
```

muypoco is pinned to 0.16 for now, so this has to wait for 0.17.

### Anything else

Clone the repo as `muypoco/` and add its parent directory to the include path.

## Develop

```
zig build test                  # all tests, normal and single-file builds
zig build examples              # build and run every example
zig build bench                 # emit benchmark, -Dbench-max-*-ns to fail on regression
```
