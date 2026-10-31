# Timers {#timers}

`#include "muypoco/timers.h"`

Node's timers. Every timer runs on one shared thread, one callback at a time, so a slow callback delays the rest. Callbacks must be `noexcept`.

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

`wait()` never returns while an interval is active.

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
