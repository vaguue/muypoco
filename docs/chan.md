# Chan {#chan}

`#include "muypoco/chan.h"`

A bounded queue with its own worker thread. One thread enqueues, the worker processes. It's a Go channel with exactly one sender and one receiver.

## Worker

```cpp
struct Write {
  std::string key;
  std::string value;
};

struct Store {
  bool init() noexcept;                // optional, runs on the worker thread first
  bool process(Write& write) noexcept; // or void
};

muypoco::Chan<Store, Write, 1024> store(/* Store constructor arguments */);
store.start();
```

`process` returning `false` stops the channel. So does `init` returning `false`, and in that case queued tasks are not processed. Both must be `noexcept`. Capacity must be a power of two.

The worker is a member (`store.worker`). The destructor stops the channel, lets the worker finish what's queued, joins the thread, and only then destroys the worker.

## Send

```cpp
store.enqueue(Write{"session", "42"});      // blocks while full; false when stopped
store.tryEnqueue(Write{"metrics", "1"});    // never blocks; false when full or stopped
```

## Backpressure

```cpp
if (!metrics.tryEnqueue(sample)) {
  dropped++;
}
```

## Stop

```cpp
store.stop();       // stop accepting; the worker drains the queue and exits
store.shutdown();   // stop() and join
```

## On the main thread

Don't call `start()`. Call `run()` from the thread that should do the work. It runs the worker loop right there and returns after `stop()`, once the queue is drained:

```cpp
struct OnMain {
  void process(Tick& tick) noexcept;   // runs on the main thread
};

muypoco::Chan<OnMain, Tick, 64> mainLoop;

int main() {
  // timers, other threads: mainLoop.enqueue(...)
  mainLoop.run();                      // blocks until someone calls mainLoop.stop()
}
```

If the main thread already has a loop of its own (epoll, a game loop, a GUI), use `drain()`. It processes what's queued right now, on the calling thread, and returns how many tasks it ran, without blocking:

```cpp
while (running) {
  pollSockets();
  mainLoop.drain();
  render();
}
```

The first `drain()` runs the worker's `init()`. Use one consumer per channel: `start()`, `run()` or `drain()`, from a single thread.

This is how you get events onto the main thread. See [events: threads](events.html#threads) and [examples/main_thread.cpp](https://github.com/vaguue/muypoco/blob/main/examples/main_thread.cpp).

## Await

Rarely needed. It blocks until the worker has processed the task, and the task must carry a `std::shared_ptr<muypoco::Completion> completion`:

```cpp
struct Write {
  std::string key;
  std::string value;
  std::shared_ptr<muypoco::Completion> completion;
};

bool ok = store.await(Write{"session", "42", nullptr});   // false when stopped or process() failed
bool ready = store.awaitInit();                           // false when init() failed
```

## Debug checks

Without `NDEBUG`, `Chan` aborts with a message when:

- a second thread enqueues (single producer);
- a second thread consumes (`start()` plus `drain()`, or `drain()` from two threads);
- the consumer enqueues into or awaits its own channel (it would deadlock once full);
- `start()` is called twice.

In release builds these checks compile to nothing.
