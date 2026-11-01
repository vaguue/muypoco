#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "handler.h"
#include "storage.h"
#include "validation.h"

extern "C" const char muypoco_events_single_file_build_allows_one_translation_unit = 1;

namespace muypoco::detail::cells {
namespace {

template <class Key, std::size_t Index>
struct slot {
  friend auto read(slot);
};

template <class Key, std::size_t Index, class Handler>
struct writer {
  friend auto read(slot<Key, Index>) {
    return std::type_identity<Handler>{};
  }
};

template <class Key, std::size_t Index, auto Unique>
consteval bool occupied() {
  return requires { read(slot<Key, Index>{}); };
}

template <class Key, auto Unique = [] {}, std::size_t Index = 0>
consteval std::size_t size() {
  if constexpr (occupied<Key, Index, Unique>()) {
    return size<Key, Unique, Index + 1>();
  }
  else {
    return Index;
  }
}

template <class Key, std::size_t Index>
using at = typename decltype(read(slot<Key, Index>{}))::type;

template <class Key, class Handler, std::size_t Count, std::size_t Index = 0>
consteval std::size_t find() {
  if constexpr (Index == Count) {
    return Count;
  }
  else if constexpr (std::is_same_v<at<Key, Index>, Handler>) {
    return Index;
  }
  else {
    return find<Key, Handler, Count, Index + 1>();
  }
}

template <class Key, class Handler, auto Unique = [] {}>
consteval std::size_t write() {
  constexpr std::size_t count = size<Key, Unique>();
  constexpr std::size_t existing = find<Key, Handler, count>();

  if constexpr (existing < count) {
    return existing;
  }
  else {
    static_cast<void>(sizeof(writer<Key, count, Handler>));
    return count;
  }
}

template <class Key, class Payload, std::size_t... Index>
void call(const Payload& payload, std::index_sequence<Index...>) noexcept {
  (at<Key, Index>{}(payload), ...);
}

}
}

namespace muypoco::detail {
namespace {

template <class Key, class Handler, class Stored, auto Unique = [] {}>
consteval std::size_t declare() {
  check_static_handler<Key, Handler>();
  check_handler<Key, Stored>();
  return cells::write<Key, Stored, Unique>();
}

template <class Key, class Stored>
void anchor() noexcept {
  record_subscription<Key>();
  record_static_handler<Key, Stored>();
}

template <class Key, auto Unique = [] {}>
void dispatch(const typename Key::payload& payload) noexcept {
  constexpr std::size_t count = cells::size<Key, Unique>();

  record_compiled_cells<Key, count>();
  cells::call<Key>(payload, std::make_index_sequence<count>{});
  storage<Key>.emit(payload);
}

}
}
