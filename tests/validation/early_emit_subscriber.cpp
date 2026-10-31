#include "events.h"

struct Data {
  int value;
};

void subscriber() {
  muypoco::events::on<"startup">([](const Data&) noexcept {
  });
}
