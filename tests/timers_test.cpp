#include "timers.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "check.h"

using namespace std::chrono_literals;
using testing::check;

void test_timeout_fires_once() {
  std::atomic<int> fired{0};

  muypoco::setTimeout([&fired]() noexcept {
    fired++;
  }, 10ms);
  muypoco::wait();
  check(fired.load() == 1, "setTimeout fires once");
}

void test_timeout_order() {
  std::mutex lock;
  std::string order;

  auto mark = [&](const char* step) noexcept {
    std::lock_guard<std::mutex> guard(lock);
    order += step;
  };

  muypoco::setTimeout([&]() noexcept {
    mark("c");
  }, 30ms);
  muypoco::setTimeout([&]() noexcept {
    mark("a");
  }, 10ms);
  muypoco::setTimeout([&]() noexcept {
    mark("b");
  }, 20ms);
  muypoco::wait();
  check(order == "abc", "timeouts fire in deadline order");
}

void test_clear_timeout() {
  std::atomic<int> fired{0};
  const int id = muypoco::setTimeout([&fired]() noexcept {
    fired++;
  }, 50ms);

  check(muypoco::clearTimeout(id), "clearTimeout on a pending timeout returns true");
  check(!muypoco::clearTimeout(id), "clearTimeout twice returns false");
  muypoco::wait();
  std::this_thread::sleep_for(80ms);
  check(fired.load() == 0, "a cleared timeout never fires");
}

void test_interval() {
  std::atomic<int> ticks{0};
  std::atomic<int> id{0};

  id = muypoco::setInterval([&]() noexcept {
    if (++ticks == 3) {
      muypoco::clearInterval(id.load());
    }
  }, 5ms);
  muypoco::wait();
  check(ticks.load() == 3, "an interval can clear itself from its callback");
}

void test_timeout_from_callback() {
  std::atomic<int> fired{0};

  muypoco::setTimeout([&fired]() noexcept {
    fired++;
    muypoco::setTimeout([&fired]() noexcept {
      fired++;
    }, 5ms);
  }, 5ms);
  muypoco::wait();
  check(fired.load() == 2, "setTimeout works from inside a callback");
}

void test_clear_all() {
  std::atomic<int> fired{0};

  for (int i = 0; i < 5; i++) {
    muypoco::setInterval([&fired]() noexcept {
      fired++;
    }, 50ms);
  }
  muypoco::clearAll();
  muypoco::wait();
  check(fired.load() == 0, "clearAll cancels every timer");
}

void test_throttle() {
  muypoco::Throttle throttle{50ms};

  check(throttle.ready(), "first call is ready");
  check(!throttle.ready(), "second call within the interval is throttled");
}

int main() {
  muypoco::log().configure(muypoco::level::off);
  muypoco::mStart();
  test_timeout_fires_once();
  test_timeout_order();
  test_clear_timeout();
  test_interval();
  test_timeout_from_callback();
  test_clear_all();
  test_throttle();
  check(muypoco::mEnd("timers").count() >= 0, "mEnd returns the elapsed time");
  return testing::finish("timers");
}
