#include <atomic>
#include <chrono>

#include "muypoco/chan.h"
#include "muypoco/events.h"
#include "muypoco/log.h"
#include "muypoco/timers.h"

using namespace std::chrono_literals;

inline thread_local const char* threadName = "main";

struct Tick {
  int number;
};

struct ReporterTask {
  int number = 0;
};

struct Reporter {
  int written = 0;

  void init() noexcept {
    threadName = "worker";
  }

  void process(ReporterTask& report) noexcept {
    this->written++;
    muypoco::log().info("[%s] report %d written", threadName, report.number);
  }
};

// Declares a bus named `app`. It expands to a struct and a constant that share the name:
// `app::on` goes through the struct and subscribes at compile time,
// `app.on` and `app.emit` go through the constant and work at runtime.
// Every bus has its own subscribers, so a "tick" on another module's bus never reaches this one.
// Limits: namespace scope only (inside a function, write the two lines it expands to by hand),
// and since a variable now hides the type, write `struct app` where you need the type itself.
MUYPOCO_BUS(app);

void announce() {
  // announce() is never called, and this handler still fires on every tick:
  // `app::on` subscribes because this line is compiled, not because it runs.
  // The handler is registered before main starts; the call itself does nothing.
  // It can't capture anything, which is why the handler that needs `reporter` uses `app.on` in main.
  app::on<"tick">([](const Tick& tick) noexcept {
    muypoco::log().info("[%s] tick %d", threadName, tick.number);
  });
}

int main() {
  muypoco::log().start();

  muypoco::Chan<Reporter, ReporterTask, 64> reporter;
  reporter.start();

  app.on<"tick">([&reporter](const Tick& tick) noexcept {
    if (tick.number % 2 == 0) {
      muypoco::log().info("[%s] tick %d goes to the worker", threadName, tick.number);
      reporter.enqueue(ReporterTask{tick.number});
    }
  });

  std::atomic<int> ticks{0};
  std::atomic<int> clock{0};

  clock = muypoco::setInterval([&]() noexcept {
    threadName = "timer";

    const int number = ++ticks;

    app.emit<"tick">(Tick{number});
    if (number == 6) {
      muypoco::clearInterval(clock);
    }
  }, 5ms);

  muypoco::wait();
  reporter.shutdown();
  muypoco::log().stop();
  muypoco::log().info("[%s] %d reports", threadName, reporter.worker.written);
  return 0;
}
