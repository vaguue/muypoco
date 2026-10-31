#pragma once

#include <cstddef>

#include "handler.h"
#include "storage.h"
#include "validation.h"

namespace muypoco::detail {

template <class Key, class Handler>
struct registrar {
  registrar() noexcept {
    storage<Key>.append({&call_stateless<typename Key::payload, Handler>, nullptr});
  }
};

template <class Key, class Handler>
inline registrar<Key, Handler> registered{};

template <class Key, class Handler, class Stored>
consteval std::size_t declare() {
  check_static_handler<Key, Handler>();
  check_handler<Key, Stored>();
  return 0;
}

template <class Key, class Stored>
void anchor() noexcept {
  static_cast<void>(&registered<Key, Stored>);
  record_subscription<Key>();
  record_static_handler<Key, Stored>();
}

template <class Key>
void dispatch(const typename Key::payload& payload) noexcept {
  storage<Key>.emit(payload);
}

}
