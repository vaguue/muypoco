#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

#include "detail/check.h"

/// @file chan.h
/// Single-producer single-consumer channel with its own worker thread.

namespace muypoco {

/// Result of one awaited task. Shared between the awaiting thread and the worker.
struct Completion {
  std::atomic<bool> done{false};
  std::atomic<bool> ok{true};

  void wait() {
    this->done.wait(false, std::memory_order_acquire);
  }

  void signal(bool success = true) {
    this->ok.store(success, std::memory_order_release);
    this->done.store(true, std::memory_order_release);
    this->done.notify_one();
  }
};

/// A task that can be used with Chan::await: it carries a `std::shared_ptr<Completion> completion`.
template <class Task>
concept awaitable_task = requires(Task task) {
  task.completion = std::shared_ptr<Completion>{};
};

namespace detail {

template <bool Enabled>
struct thread_owner {
  std::atomic<std::thread::id> owner{};

  void claim(const char* message) noexcept {
    const std::thread::id self = std::this_thread::get_id();
    std::thread::id expected{};

    if (!this->owner.compare_exchange_strong(expected, self, std::memory_order_relaxed) && expected != self) {
      violated(message);
    }
  }

  bool is(std::thread::id other) const noexcept {
    return this->owner.load(std::memory_order_relaxed) == other;
  }
};

template <>
struct thread_owner<false> {
  void claim(const char*) noexcept {
  }

  bool is(std::thread::id) const noexcept {
    return false;
  }
};

}

/// Bounded single-producer single-consumer queue drained by its own thread.
///
/// The worker is a member, so it is destroyed only after the thread has been joined:
/// @code
/// struct Db {
///   bool init() noexcept;                  // optional; returning false stops the channel
///   bool process(Query& query) noexcept;   // or void; returning false stops the channel
/// };
///
/// muypoco::Chan<Db, Query, 1024> db;
/// db.start();
/// db.enqueue(Query{...});
/// @endcode
/// One thread enqueues, the worker thread processes. Debug builds abort when a second thread enqueues,
/// or when the worker enqueues into or awaits its own channel; release builds check nothing.
template <class Worker, class Task, std::size_t Capacity>
struct Chan {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "muypoco: Chan capacity must be a power of two");
  static_assert(std::is_default_constructible_v<Task> && std::is_move_assignable_v<Task>,
                "muypoco: Chan tasks must be default constructible and move assignable");

  static constexpr std::size_t Mask = Capacity - 1;

  Worker worker;
  std::array<Task, Capacity> buf;

  alignas(64) std::atomic<std::size_t> head{0};
  alignas(64) std::atomic<std::size_t> tail{0};
  alignas(64) std::atomic<std::uint32_t> notEmptyGate{0};
  alignas(64) std::atomic<std::uint32_t> notFullGate{0};
  alignas(64) std::atomic<bool> stopped{false};
  alignas(64) std::atomic<int> initState{-1};

  [[no_unique_address]] detail::thread_owner<detail::checks> producer;
  std::thread th;

  /// Arguments are forwarded to the worker's constructor.
  template <class... Arguments>
  explicit Chan(Arguments&&... arguments) : worker(std::forward<Arguments>(arguments)...) {
  }

  Chan(const Chan&) = delete;
  Chan& operator=(const Chan&) = delete;

  /// Starts the worker thread. Call once.
  void start() {
    detail::check(!this->th.joinable(), "muypoco: Chan::start called twice");
    this->th = std::thread([this] {
      this->run();
    });
  }

  /// Tasks waiting in the queue.
  std::uint32_t occupancy() const {
    return static_cast<std::uint32_t>(this->tail.load(std::memory_order_relaxed) -
                                      this->head.load(std::memory_order_relaxed));
  }

  bool isStopped() const {
    return this->stopped.load(std::memory_order_relaxed);
  }

  /// Blocks until the worker's init() returned. False when init() failed.
  bool awaitInit() {
    this->initState.wait(-1, std::memory_order_acquire);
    return this->initState.load(std::memory_order_acquire) == 1;
  }

  /// Stops accepting tasks. The worker finishes what is queued and exits.
  void stop() {
    this->stopped.store(true, std::memory_order_release);
    this->notEmptyGate.fetch_add(1, std::memory_order_release);
    this->notEmptyGate.notify_all();
    this->notFullGate.fetch_add(1, std::memory_order_release);
    this->notFullGate.notify_all();
  }

  /// stop() and wait for the worker thread to exit.
  void shutdown() {
    this->stop();
    if (this->th.joinable()) {
      this->th.join();
    }
  }

  /// Enqueues without blocking. False when the queue is full or stopped.
  bool tryEnqueue(Task t) {
    this->checkProducer();

    if (this->stopped.load(std::memory_order_acquire)) {
      return false;
    }

    const std::size_t tl = this->tail.load(std::memory_order_relaxed);

    if (tl - this->head.load(std::memory_order_acquire) >= Capacity) {
      return false;
    }

    this->publish(tl, std::move(t));
    return true;
  }

  /// Enqueues, blocking while the queue is full. False when stopped.
  bool enqueue(Task t) {
    this->checkProducer();

    const std::size_t tl = this->tail.load(std::memory_order_relaxed);

    for (;;) {
      if (this->stopped.load(std::memory_order_acquire)) {
        return false;
      }
      if (tl - this->head.load(std::memory_order_acquire) < Capacity) {
        break;
      }

      const std::uint32_t gate = this->notFullGate.load(std::memory_order_acquire);

      if (tl - this->head.load(std::memory_order_acquire) < Capacity || this->stopped.load(std::memory_order_acquire)) {
        continue;
      }
      this->notFullGate.wait(gate, std::memory_order_acquire);
    }

    this->publish(tl, std::move(t));
    return true;
  }

  /// Enqueues and blocks until the worker processed the task. False when stopped or when process() failed.
  bool await(Task t) requires awaitable_task<Task> {
    std::shared_ptr<Completion> completion = std::make_shared<Completion>();

    t.completion = completion;

    if (!this->enqueue(std::move(t))) {
      return false;
    }

    completion->wait();
    return completion->ok.load(std::memory_order_acquire);
  }

  ~Chan() {
    this->shutdown();
  }

  void checkProducer() {
    if constexpr (detail::checks) {
      if (this->th.get_id() == std::this_thread::get_id()) {
        detail::violated("muypoco: Chan worker enqueued into its own channel; it would deadlock once the queue is full");
      }
      this->producer.claim("muypoco: Chan is single-producer but a second thread enqueued");
    }
  }

  void publish(std::size_t tl, Task&& t) {
    this->buf[tl & Mask] = std::move(t);
    this->tail.store(tl + 1, std::memory_order_release);
    this->notEmptyGate.fetch_add(1, std::memory_order_release);
    this->notEmptyGate.notify_one();
  }

  void setInitState(int state) {
    this->initState.store(state, std::memory_order_release);
    this->initState.notify_all();
  }

  bool init() noexcept {
    if constexpr (requires(Worker& w) { w.init(); }) {
      static_assert(requires(Worker& w) { { w.init() } noexcept; },
                    "muypoco: Worker::init() must be noexcept; report failure by returning false");

      if constexpr (std::is_same_v<decltype(this->worker.init()), bool>) {
        const bool ok = this->worker.init();
        this->setInitState(ok ? 1 : 0);
        if (!ok) {
          this->stop();
        }
        return ok;
      }
      else {
        this->worker.init();
        this->setInitState(1);
        return true;
      }
    }
    else {
      this->setInitState(1);
      return true;
    }
  }

  bool process(Task& t) noexcept {
    static_assert(requires(Worker& w, Task& task) { { w.process(task) } noexcept; },
                  "muypoco: Worker::process(Task&) must be noexcept; report failure by returning false");

    if constexpr (std::is_same_v<decltype(this->worker.process(t)), bool>) {
      return this->worker.process(t);
    }
    else {
      this->worker.process(t);
      return true;
    }
  }

  void run() noexcept {
    const bool ready = this->init();

    std::size_t hd = this->head.load(std::memory_order_relaxed);

    for (;;) {
      if (hd == this->tail.load(std::memory_order_acquire)) {
        if (this->stopped.load(std::memory_order_acquire)) {
          if (hd == this->tail.load(std::memory_order_acquire)) {
            return;
          }
          continue;
        }

        const std::uint32_t gate = this->notEmptyGate.load(std::memory_order_acquire);

        if (hd != this->tail.load(std::memory_order_acquire) || this->stopped.load(std::memory_order_acquire)) {
          continue;
        }
        this->notEmptyGate.wait(gate, std::memory_order_acquire);
        continue;
      }

      Task t = std::move(this->buf[hd & Mask]);
      hd += 1;
      this->head.store(hd, std::memory_order_release);
      this->notFullGate.fetch_add(1, std::memory_order_release);
      this->notFullGate.notify_one();

      const bool ok = ready && this->process(t);

      if (!ok) {
        this->stop();
      }
      if constexpr (awaitable_task<Task>) {
        if (t.completion != nullptr) {
          t.completion->signal(ok);
        }
      }
    }
  }
};

}
