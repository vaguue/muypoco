#include <atomic>
#include <chrono>
#include <memory>

#include "muypoco/chan.h"
#include "muypoco/events.h"
#include "muypoco/log.h"
#include "muypoco/timers.h"

using namespace std::chrono_literals;

struct Tick {
  int number;
};

struct Report {
  int number = 0;
  std::shared_ptr<muypoco::Completion> completion;
};

struct Reporter {
  int written = 0;

  void process(Report& report) noexcept {
    this->written++;
    muypoco::log().info("report %d written", report.number);
  }
};

struct app : muypoco::bus<app> {};
inline constexpr muypoco::handle<app> app{};

void announce() {
  app::on<"tick">([](const Tick& tick) noexcept {
    muypoco::log().info("tick %d", tick.number);
  });
}

int main() {
  muypoco::log().start();

  muypoco::Chan<Reporter, Report, 64> reporter;
  reporter.start();

  app.on<"tick">([&reporter](const Tick& tick) noexcept {
    if (tick.number % 2 == 0) {
      reporter.enqueue(Report{tick.number, nullptr});
    }
  });

  std::atomic<int> ticks{0};
  std::atomic<int> clock{0};

  clock = muypoco::setInterval([&]() noexcept {
    const int number = ++ticks;

    app.emit<"tick">(Tick{number});
    if (number == 6) {
      muypoco::clearInterval(clock);
    }
  }, 5ms);

  muypoco::wait();
  reporter.shutdown();
  muypoco::log().info("%d reports", reporter.worker.written);
  muypoco::log().stop();
  return 0;
}
