#include <atomic>
#include <chrono>
#include <cstdio>

#include "muypoco/timers.h"

using namespace std::chrono_literals;

int main() {
  std::atomic<int> beats{0};
  std::atomic<int> heartbeat{0};

  muypoco::mStart();

  heartbeat = muypoco::setInterval([&]() noexcept {
    std::printf("beat %d\n", ++beats);
  }, 10ms);

  muypoco::setTimeout([&]() noexcept {
    muypoco::clearInterval(heartbeat);
    std::printf("stopped after %d beats\n", beats.load());
  }, 55ms);

  const int cancelled = muypoco::setTimeout([]() noexcept {
    std::printf("never printed\n");
  }, 20ms);
  muypoco::clearTimeout(cancelled);

  muypoco::wait();
  muypoco::mEnd("heartbeat");

  muypoco::Throttle throttle{100ms};
  int allowed = 0;

  for (int i = 0; i < 1000; i++) {
    if (throttle.ready()) {
      allowed++;
    }
  }
  std::printf("throttle let %d of 1000 through\n", allowed);
  return 0;
}
