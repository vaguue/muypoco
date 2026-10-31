#pragma once

#include <type_traits>
#include <utility>

#include "handler.h"
#include "storage.h"
#include "validation.h"

namespace muypoco::detail {

template <class Key, class Stored, class Handler>
void subscribe(Handler&& handler) noexcept {
  using Payload = typename Key::payload;

  static_cast<void>(check_handler<Key, std::decay_t<Handler>>());
  record_subscription<Key>();

  if constexpr (stateless<Stored>) {
    storage<Key>.append({&call_stateless<Payload, Stored>, nullptr});
  }
  else {
    storage<Key>.append({&call_stateful<Payload, Stored>, new Stored(std::forward<Handler>(handler))});
  }
}

}
