#include <cstdio>

#include "events.h"

struct Data {
  int value;
};

int fired = 0;

void emit_now() {
  muypoco::events.emit<"late">(Data{1});
}

template <class Payload>
void subscribe_later() {
  muypoco::events::on<"late">([](const Payload&) noexcept {
    fired++;
  });
}

template <class Payload>
struct wiring {
  static void run() {
    subscribe_later<Payload>();
  }
};

int main() {
  emit_now();
  wiring<Data>::run();
  std::printf("fired=%d\n", fired);
}
