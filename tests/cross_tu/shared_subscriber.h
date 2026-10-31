#pragma once

#include "cross.h"

inline void shared_subscriber() {
  cross::on<"ping">([](const ping& payload) noexcept {
    hits_shared += payload.value;
  });
}
