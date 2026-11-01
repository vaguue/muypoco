#include "chan.h"

#include <atomic>
#include <memory>
#include <thread>

#include "check.h"

using testing::check;

struct Job {
  int value = 0;
  std::shared_ptr<muypoco::Completion> completion;
};

struct Summer {
  int* total;

  bool process(Job& job) noexcept {
    *this->total += job.value;
    return job.value >= 0;
  }
};

struct Plain {
  int value = 0;
};

struct Counter {
  std::atomic<int> seen{0};

  void process(Plain& task) noexcept {
    this->seen.fetch_add(task.value, std::memory_order_relaxed);
  }
};

struct Failing {
  int processed = 0;

  bool init() noexcept {
    return false;
  }

  bool process(Job&) noexcept {
    this->processed++;
    return true;
  }
};

struct Slow {
  std::atomic<bool>* release;
  std::atomic<int>* processed;

  void process(Plain&) noexcept {
    this->release->wait(false, std::memory_order_acquire);
    this->processed->fetch_add(1, std::memory_order_relaxed);
  }
};

struct Guarded {
  std::unique_ptr<int> state = std::make_unique<int>(0);

  void process(Plain& task) noexcept {
    *this->state += task.value;
  }
};

void test_enqueue_and_drain() {
  int total = 0;

  {
    muypoco::Chan<Summer, Job, 8> chan(&total);
    chan.start();
    check(chan.awaitInit(), "init without Worker::init succeeds");
    for (int i = 1; i <= 100; i++) {
      check(chan.enqueue(Job{i, nullptr}), "enqueue blocks while full and succeeds");
    }
  }
  check(total == 5050, "destructor drains every queued task before joining");
}

void test_await() {
  int total = 0;
  muypoco::Chan<Summer, Job, 4> chan(&total);
  chan.start();

  check(chan.await(Job{5, nullptr}), "await returns process() result");
  check(total == 5, "await returns after the task was processed");
  check(!chan.await(Job{-1, nullptr}), "await reports a failed task");
  check(chan.isStopped(), "a failed task stops the channel");
  check(!chan.enqueue(Job{1, nullptr}), "enqueue on a stopped channel returns false");
}

void test_void_process() {
  muypoco::Chan<Counter, Plain, 16> chan;
  chan.start();

  for (int i = 0; i < 10; i++) {
    chan.enqueue(Plain{1});
  }
  chan.shutdown();
  check(chan.worker.seen.load() == 10, "void process() counts as success");
}

void test_try_enqueue_full() {
  std::atomic<bool> release{false};
  std::atomic<int> processed{0};
  muypoco::Chan<Slow, Plain, 2> chan(&release, &processed);
  chan.start();

  check(chan.tryEnqueue(Plain{}), "first tryEnqueue succeeds");
  while (chan.occupancy() != 0) {
    std::this_thread::yield();
  }
  check(chan.tryEnqueue(Plain{}), "second tryEnqueue fills the queue");
  check(chan.tryEnqueue(Plain{}), "third tryEnqueue fills the queue");
  check(!chan.tryEnqueue(Plain{}), "tryEnqueue on a full queue returns false");

  release.store(true, std::memory_order_release);
  release.notify_all();
  chan.shutdown();
  check(processed.load() == 3, "every accepted task is processed");
}

void test_failed_init() {
  muypoco::Chan<Failing, Job, 4> chan;
  chan.start();

  check(!chan.awaitInit(), "awaitInit reports a failed init");
  check(!chan.await(Job{1, nullptr}), "await after a failed init returns false");
  chan.shutdown();
  check(chan.worker.processed == 0, "no task is processed after a failed init");
}

void test_worker_outlives_thread() {
  for (int round = 0; round < 50; round++) {
    muypoco::Chan<Guarded, Plain, 64> chan;
    chan.start();
    for (int i = 0; i < 64; i++) {
      chan.enqueue(Plain{1});
    }
  }
  check(true, "worker state is destroyed only after the thread joined");
}

struct Recorder {
  std::thread::id* seen;
  int processed = 0;
  bool initialized = false;

  void init() noexcept {
    this->initialized = true;
  }

  void process(Plain&) noexcept {
    *this->seen = std::this_thread::get_id();
    this->processed++;
  }
};

void test_drain() {
  std::thread::id seen;
  muypoco::Chan<Recorder, Plain, 16> chan(&seen);

  check(chan.drain() == 0, "drain on an empty queue returns 0 without blocking");
  check(chan.worker.initialized, "the first drain runs init");

  std::thread producer([&chan] {
    for (int i = 0; i < 100; i++) {
      chan.enqueue(Plain{1});
    }
  });

  int drained = 0;

  while (drained < 100) {
    drained += static_cast<int>(chan.drain());
    std::this_thread::yield();
  }
  producer.join();

  check(drained == 100 && chan.worker.processed == 100, "drain processes every task exactly once");
  check(seen == std::this_thread::get_id(), "drain runs process() on the calling thread");
}

void test_run_on_caller() {
  std::thread::id seen;
  muypoco::Chan<Recorder, Plain, 16> chan(&seen);

  std::thread producer([&chan] {
    for (int i = 0; i < 10; i++) {
      chan.enqueue(Plain{1});
    }
    chan.stop();
  });

  chan.run();
  producer.join();

  check(chan.worker.processed == 10, "run on the caller drains the queue before returning");
  check(seen == std::this_thread::get_id(), "run runs process() on the calling thread");
}

int main() {
  test_enqueue_and_drain();
  test_drain();
  test_run_on_caller();
  test_await();
  test_void_process();
  test_try_enqueue_full();
  test_failed_init();
  test_worker_outlives_thread();
  return testing::finish("chan");
}
