# Timers {#timers}

`#include "muypoco/timers.h"`

Node's timers, with one difference: every timer runs on its own thread. A slow callback never delays another timer, and none of them run on your main thread. Callbacks of different timers can run at the same time, so shared state needs a lock or an atomic. Callbacks must be `noexcept`.

This is meant for a handful of long-lived jobs (heartbeats, flushes, cleanups), not thousands of short-lived timeouts.

## Timeout

```cpp
int id = muypoco::setTimeout([]() noexcept {
  reconnect();
}, 500ms);

muypoco::clearTimeout(id);   // false if it already fired
```

## Interval

```cpp
std::atomic<int> heartbeat{0};

heartbeat = muypoco::setInterval([&]() noexcept {
  ping();
}, 1s);

muypoco::clearInterval(heartbeat);
```

The period is at least 1ms. A late tick isn't repeated to catch up. An interval may clear itself, and callbacks may start new timers.

## Wait

```cpp
muypoco::setTimeout(flush, 100ms);
muypoco::wait();       // returns once nothing is pending or running
muypoco::clearAll();
```

`wait()` never returns while an interval is active. Every timer thread is joined: clearing a timer stops its thread, and at exit the pending timers are cancelled and joined.

## Measure

```cpp
muypoco::mStart();
load();
muypoco::mEnd("load");   // logs "[*] load took 42ms" and returns 42ms
```

The stopwatch is per thread.

## Throttle

```cpp
muypoco::Throttle throttle{1s};

if (throttle.ready()) {
  report();
}
```

Lets one call through per interval. Not thread-safe, so give each thread its own.
