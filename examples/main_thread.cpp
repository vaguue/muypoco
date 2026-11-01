#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "muypoco/chan.h"
#include "muypoco/events.h"
#include "muypoco/timers.h"

using namespace std::chrono_literals;

struct Tick {
  int number;
};

std::thread::id mainThread;

struct OnMain {
  void process(Tick& tick) noexcept {
    const bool onMain = std::this_thread::get_id() == mainThread;
    std::printf("tick %d handled on the %s thread\n", tick.number, onMain ? "main" : "wrong");
  }
};

muypoco::Chan<OnMain, Tick, 64> mainLoop;

MUYPOCO_BUS(app);

void forwardToMain() {
  app::on<"tick">([](const Tick& tick) noexcept {
    mainLoop.enqueue(tick);
  });
}

int main() {
  mainThread = std::this_thread::get_id();

  std::atomic<int> count{0};

  muypoco::setInterval([&count]() noexcept {
    const int number = ++count;

    app.emit<"tick">(Tick{number});
    if (number == 3) {
      mainLoop.stop();
    }
  }, 5ms);

  mainLoop.run();
  muypoco::clearAll();
  std::printf("main loop exited\n");
  return 0;
}
