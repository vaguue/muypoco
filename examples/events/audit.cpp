#include <cstdio>

#include "muypoco/events.h"
#include "request.h"

void audit(const request& incoming) noexcept {
  if (incoming.id % 2 == 0) {
    std::printf("audit: request %llu\n", static_cast<unsigned long long>(incoming.id));
  }
}

void audit_subscriptions() {
  muypoco::events::on<"request", &audit>();
}
