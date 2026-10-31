#include "cross.h"
#include "shared_subscriber.h"

void subscriber_b() {
  cross::on<"ping">([](const ping& payload) noexcept {
    hits_b += payload.value;
  });
}
