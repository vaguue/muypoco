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

Modules can have their own bus with `MUYPOCO_BUS(net);`, then `net::on`, `net.on` and `net.emit`.

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

### All together

A timer emits ticks, a handler picks them up, and a Chan worker writes reports on its own thread:

```cpp
#include <atomic>
#include <chrono>

#include "muypoco/chan.h"
#include "muypoco/events.h"
#include "muypoco/log.h"
#include "muypoco/timers.h"

using namespace std::chrono_literals;

struct Tick {
  int number;
};

struct ReporterTask {
  int number = 0;
};

struct Reporter {
  void process(ReporterTask& report) noexcept {
    muypoco::log().info("report %d written", report.number);   // worker thread
  }
};

MUYPOCO_BUS(app);

void announce() {
  app::on<"tick">([](const Tick& tick) noexcept {               // never called, still subscribed
    muypoco::log().info("tick %d", tick.number);                // timer thread
  });
}

int main() {
  muypoco::Chan<Reporter, ReporterTask, 64> reporter;
  reporter.start();

  app.on<"tick">([&reporter](const Tick& tick) noexcept {
    reporter.enqueue(ReporterTask{tick.number});                // timer thread
  });

  std::atomic<int> ticks{0};
  std::atomic<int> clock{0};

  clock = muypoco::setInterval([&]() noexcept {
    const int number = ++ticks;

    app.emit<"tick">(Tick{number});                             // timer thread
    if (number == 3) {
      muypoco::clearInterval(clock);
    }
  }, 5ms);

  muypoco::wait();
  reporter.shutdown();
}
```

Handlers run on the thread that emits, here the timer's. The Chan moves the work to its worker. The full version, with each log line labeled by thread, is [examples/pipeline.cpp](https://github.com/vaguue/muypoco/blob/main/examples/pipeline.cpp).

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

This needs Zig 0.16.0 or newer. For the single-file build, compile the program as one translation unit and add `exe.root_module.addCMacro("MUYPOCO_EVENTS_INLINE", "1");`. If you're writing a library that uses muypoco events, ship one entry `.cpp` and let the app pick the mode; see [libraries](https://vaguue.github.io/muypoco/events.html#libraries).

In a normal build, a file that only contains `::on` subscriptions must be linked as an object (`exe.root_module.addObject(...)`), not through a static library, or the linker drops it. See [linking](https://vaguue.github.io/muypoco/events.html#linking).

#### cxx_modules

Zig 0.17 (still in development) can keep fetched packages in a project folder set by `ZIG_LOCAL_PKG_DIR`. Which finally makes this a real command:

```
ZIG_LOCAL_PKG_DIR=cxx_modules zig build
```

It needs Zig 0.17, which is still in development.

### Anything else

Clone the repo as `muypoco/` and add its parent directory to the include path.

## Develop

```
zig build test                  # all tests, normal and single-file builds
zig build examples              # build and run every example
zig build bench                 # emit benchmark, -Dbench-max-*-ns to fail on regression
```
