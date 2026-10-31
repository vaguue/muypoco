#include "events.h"

#include <cstring>

#include "../check.h"

using testing::check;

struct Data {
  int value;
};

void subscriber() {
  muypoco::events::on<"pong">([](const Data&) noexcept {
  });
}

void emitter() {
  muypoco::events.emit<"ping">(Data{1});
}

int main() {
  const auto [ok, message] = muypoco::validate();
  check(!ok, "validate reports a subscription nobody emits");
  check(std::strstr(message, "\"pong\"") != nullptr, "the report names the event");
  check(std::strstr(message, "Data") != nullptr, "the report names the payload type");
  return testing::finish("validation report");
}
