#include "events.h"

struct Data {
  int value;
};

void subscriber() {
  muypoco::events::on<"split">([](const Data&) noexcept {
  });
}
