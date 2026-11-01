#include <atomic>
#include <chrono>

#include "muypoco/events.h"
#include "muypoco/timers.h"

#include "dicker/dicker.h"

#include "../../check.h"

using namespace std::chrono_literals;
using testing::check;

MUYPOCO_BUS(app);

struct Tick {
  int number;
};

int readies = 0;
int ticks = 0;

void wiring() {
  dicker::events::on<"ready">([](const dicker::Ready& ready) noexcept {
    readies += ready.connections;
  });
  app::on<"tick">([](const Tick&) noexcept {
    ticks++;
  });
}

int main() {
  dicker::start();
  check(readies == 3, "the app receives dicker's event");
  check(dicker::audited == 1, "dicker's subscription-only file was linked in");

  std::atomic<int> count{0};
  std::atomic<int> clock{0};

  clock = muypoco::setInterval([&]() noexcept {
    const int number = ++count;

    app.emit<"tick">(Tick{number});
    if (number == 3) {
      muypoco::clearInterval(clock);
    }
  }, 2ms);
  muypoco::wait();
  check(ticks == 3, "the app's own events work next to dicker's");

#ifdef MUYPOCO_EVENTS_INLINE
  return testing::finish("library (single-file build)");
#else
  return testing::finish("library (normal build)");
#endif
}
