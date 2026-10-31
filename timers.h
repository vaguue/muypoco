#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "log.h"

/// @file timers.h
/// setTimeout / setInterval, one thread per timer, plus small timing helpers.

namespace muypoco {

namespace detail::timers {

using clock = std::chrono::steady_clock;

inline thread_local bool onTimerThread = false;

struct timer {
  std::mutex lock;
  std::condition_variable wake;
  bool cancelled = false;
  bool fired = false;
  bool finished = false;
  std::thread thread;
};

struct registry {
  std::mutex lock;
  std::condition_variable idle;
  std::unordered_map<int, std::shared_ptr<timer>> live;
  int last_id = 0;
  int running = 0;

  static registry& get() {
    static registry instance;
    return instance;
  }

  ~registry() {
    this->stopAll();

    if (onTimerThread) {
      std::lock_guard<std::mutex> guard(this->lock);

      for (auto& [id, owned] : this->live) {
        owned->thread.detach();
      }
      return;
    }

    this->wait();
  }

  void reapLocked() {
    for (auto it = this->live.begin(); it != this->live.end();) {
      timer& candidate = *it->second;

      if (candidate.finished && candidate.thread.get_id() != std::this_thread::get_id()) {
        candidate.thread.join();
        it = this->live.erase(it);
      }
      else {
        ++it;
      }
    }
  }

  int start(std::function<void()> callback, clock::duration period, bool repeat) {
    const std::shared_ptr<timer> created = std::make_shared<timer>();
    std::lock_guard<std::mutex> guard(this->lock);

    this->reapLocked();

    const int id = ++this->last_id;

    this->live.emplace(id, created);
    this->running++;
    created->thread = std::thread([this, created, callback = std::move(callback), period, repeat] {
      this->run(*created, callback, period, repeat);
    });
    return id;
  }

  void run(timer& self, const std::function<void()>& callback, clock::duration period, bool repeat) {
    onTimerThread = true;

    clock::time_point next = clock::now();

    for (;;) {
      next += period;

      const clock::time_point now = clock::now();

      if (next < now) {
        next = now;
      }

      {
        std::unique_lock<std::mutex> guard(self.lock);

        if (self.wake.wait_until(guard, next, [&self] { return self.cancelled; })) {
          break;
        }
        if (!repeat) {
          self.fired = true;
        }
      }

      callback();

      if (!repeat) {
        break;
      }
    }

    std::lock_guard<std::mutex> guard(this->lock);
    self.finished = true;
    this->running--;

    if (this->running == 0) {
      this->idle.notify_all();
    }
  }

  bool stop(int id) {
    std::shared_ptr<timer> found;

    {
      std::lock_guard<std::mutex> guard(this->lock);
      this->reapLocked();

      const auto it = this->live.find(id);

      if (it == this->live.end() || it->second->finished) {
        return false;
      }
      found = it->second;
    }

    std::lock_guard<std::mutex> guard(found->lock);

    if (found->cancelled || found->fired) {
      return false;
    }
    found->cancelled = true;
    found->wake.notify_all();
    return true;
  }

  void stopAll() {
    std::lock_guard<std::mutex> guard(this->lock);

    for (auto& [id, owned] : this->live) {
      std::lock_guard<std::mutex> timerGuard(owned->lock);
      owned->cancelled = true;
      owned->wake.notify_all();
    }
  }

  void wait() {
    std::unique_lock<std::mutex> guard(this->lock);
    this->idle.wait(guard, [this] {
      return this->running == 0;
    });
    this->reapLocked();
  }
};

template <class Callback>
consteval bool checkCallback() {
  static_assert(std::is_nothrow_invocable_v<std::decay_t<Callback>&>,
                "muypoco: timer callbacks must be noexcept, e.g. [] () noexcept { ... }");
  return true;
}

}

/// Calls `callback` once after `delay`, on the timer's own thread. Returns an id for clearTimeout.
///
/// Every timer gets its own thread, so a slow callback never delays another timer,
/// but callbacks of different timers can run at the same time.
template <class Callback>
int setTimeout(Callback&& callback, std::chrono::milliseconds delay) {
  static_cast<void>(detail::timers::checkCallback<Callback>());
  return detail::timers::registry::get().start(std::forward<Callback>(callback), delay, false);
}

/// Calls `callback` every `period` (at least 1ms) until clearInterval, on the timer's own thread. Returns an id for clearInterval.
/// A late tick is not repeated to catch up.
template <class Callback>
int setInterval(Callback&& callback, std::chrono::milliseconds period) {
  static_cast<void>(detail::timers::checkCallback<Callback>());
  return detail::timers::registry::get().start(std::forward<Callback>(callback), std::max(period, std::chrono::milliseconds(1)), true);
}

/// Cancels a timeout. False when it already fired or never existed.
inline bool clearTimeout(int id) {
  return detail::timers::registry::get().stop(id);
}

/// Cancels an interval. False when it never existed or was already cleared.
inline bool clearInterval(int id) {
  return detail::timers::registry::get().stop(id);
}

/// Cancels every timer.
inline void clearAll() {
  detail::timers::registry::get().stopAll();
}

/// Blocks until no timer is pending or running. Never returns while an interval is active.
inline void wait() {
  detail::timers::registry::get().wait();
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
