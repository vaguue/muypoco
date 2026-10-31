#include "cross.h"
#include "shared_subscriber.h"

void subscriber_a() {
  cross::on<"ping">([](const ping& payload) noexcept {
    hits_a += payload.value;
  });
}
