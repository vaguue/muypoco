#include "events.h"

struct Data {
  int value;
};

void subscriber() {
  muypoco::events::on<"dta">([](const Data&) noexcept {
  });
}

int main() {
  muypoco::events.emit<"data">(Data{1});
  return 0;
}
