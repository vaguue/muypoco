#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "log.h"

/// @file timers.h
/// setTimeout / setInterval on one shared timer thread, plus small timing helpers.

namespace muypoco {

namespace detail::timers {

using clock = std::chrono::steady_clock;

struct timer {
  clock::duration period;
  bool repeat;
  std::shared_ptr<std::function<void()>> callback;
};

struct deadline {
  clock::time_point due;
  int id;

  bool operator>(const deadline& other) const {
    return this->due > other.due || (this->due == other.due && this->id > other.id);
  }
};

struct scheduler {
  std::mutex lock;
  std::condition_variable wake;
  std::condition_variable idle;
  std::unordered_map<int, timer> active;
  std::priority_queue<deadline, std::vector<deadline>, std::greater<deadline>> queue;
  std::thread worker;
  int last_id = 0;
  bool busy = false;
  bool stopping = false;

  static scheduler& get() {
    static scheduler instance;
    return instance;
  }

  ~scheduler() {
    {
      std::lock_guard<std::mutex> guard(this->lock);
      this->stopping = true;
    }
    this->wake.notify_all();

    if (!this->worker.joinable()) {
      return;
    }
    if (this->worker.get_id() == std::this_thread::get_id()) {
      this->worker.detach();
    }
    else {
      this->worker.join();
    }
  }

  int start(std::function<void()> callback, clock::duration period, bool repeat) {
    std::lock_guard<std::mutex> guard(this->lock);

    if (!this->worker.joinable()) {
      this->worker = std::thread([this] {
        this->run();
      });
    }

    const int id = ++this->last_id;

    this->active.emplace(id, timer{period, repeat, std::make_shared<std::function<void()>>(std::move(callback))});
    this->queue.push({clock::now() + period, id});
    this->wake.notify_one();
    return id;
  }

  bool settled() const {
    return this->active.empty() && !this->busy;
  }

  bool stop(int id) {
    std::lock_guard<std::mutex> guard(this->lock);
    const bool erased = this->active.erase(id) > 0;

    if (this->settled()) {
      this->idle.notify_all();
    }
    return erased;
  }

  void stopAll() {
    std::lock_guard<std::mutex> guard(this->lock);
    this->active.clear();

    if (this->settled()) {
      this->idle.notify_all();
    }
  }

  void wait() {
    std::unique_lock<std::mutex> guard(this->lock);
    this->idle.wait(guard, [this] {
      return this->settled();
    });
  }

  void run() {
    std::unique_lock<std::mutex> guard(this->lock);

    while (!this->stopping) {
      if (this->queue.empty()) {
        this->wake.wait(guard);
        continue;
      }

      const deadline next = this->queue.top();
      const auto found = this->active.find(next.id);

      if (found == this->active.end()) {
        this->queue.pop();
        continue;
      }
      if (clock::now() < next.due) {
        this->wake.wait_until(guard, next.due);
        continue;
      }

      this->queue.pop();

      const std::shared_ptr<std::function<void()>> callback = found->second.callback;

      if (found->second.repeat) {
        this->queue.push({std::max(next.due + found->second.period, clock::now()), next.id});
      }
      else {
        this->active.erase(found);
      }

      this->busy = true;
      guard.unlock();
      (*callback)();
      guard.lock();
      this->busy = false;

      if (this->settled()) {
        this->idle.notify_all();
      }
    }
  }
};

template <class Callback>
consteval bool checkCallback() {
  static_assert(std::is_nothrow_invocable_v<std::decay_t<Callback>&>,
                "muypoco: timer callbacks must be noexcept, e.g. [] () noexcept { ... }");
  return true;
}

}

/// Calls `callback` once after `delay`. Returns an id for clearTimeout.
///
/// All timers share one thread and run there one after another, like callbacks on Node's event loop:
/// a slow callback delays the others.
template <class Callback>
int setTimeout(Callback&& callback, std::chrono::milliseconds delay) {
  static_cast<void>(detail::timers::checkCallback<Callback>());
  return detail::timers::scheduler::get().start(std::forward<Callback>(callback), delay, false);
}

/// Calls `callback` every `period` (at least 1ms) until clearInterval. Returns an id for clearInterval.
/// A late tick is not repeated to catch up.
template <class Callback>
int setInterval(Callback&& callback, std::chrono::milliseconds period) {
  static_cast<void>(detail::timers::checkCallback<Callback>());
  return detail::timers::scheduler::get().start(std::forward<Callback>(callback), std::max(period, std::chrono::milliseconds(1)), true);
}

/// Cancels a timeout. False when it already fired or never existed.
inline bool clearTimeout(int id) {
  return detail::timers::scheduler::get().stop(id);
}

/// Cancels an interval. False when it never existed or was already cleared.
inline bool clearInterval(int id) {
  return detail::timers::scheduler::get().stop(id);
}

/// Cancels every timer.
inline void clearAll() {
  detail::timers::scheduler::get().stopAll();
}

/// Blocks until no timer is pending or running. Never returns while an interval is active.
inline void wait() {
  detail::timers::scheduler::get().wait();
}

inline thread_local std::chrono::steady_clock::time_point measureStart;

/// Starts the per-thread stopwatch used by mEnd.
inline void mStart() {
  measureStart = std::chrono::steady_clock::now();
}

/// Logs "<tag> took <n>ms" at info level since this thread's last mStart, and returns the duration.
inline std::chrono::milliseconds mEnd(const char* tag) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - measureStart);
  log().info("%s took %lldms", tag, static_cast<long long>(elapsed.count()));
  return elapsed;
}

/// Lets something happen at most once per `interval`: `if (throttle.ready()) { ... }`.
/// Not thread-safe; give each thread its own.
struct Throttle {
  std::chrono::steady_clock::duration interval;
  std::chrono::steady_clock::time_point last{};

  bool ready() {
    const auto now = std::chrono::steady_clock::now();

    if (now - this->last < this->interval) {
      return false;
    }

    this->last = now;
    return true;
  }
};

}
