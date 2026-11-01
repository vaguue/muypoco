#pragma once

#include <cstddef>
#include <string_view>

#ifndef NDEBUG
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#endif

namespace muypoco {

/// Result of muypoco::validate(): `auto [ok, message] = muypoco::validate();`
struct validation {
  bool ok;
  const char* message;
};

}

namespace muypoco::detail {

#ifndef NDEBUG

template <class Type>
constexpr std::string_view type_name() {
  const std::string_view full = __PRETTY_FUNCTION__;
  const std::string_view marker = "Type = ";
  const std::size_t start = full.find(marker) + marker.size();
  const std::size_t end = full.find_last_of(']');
  return full.substr(start, end - start);
}

template <class Type>
inline constexpr char identity = 0;

enum struct record_kind {
  emission,
  subscription,
  static_handler,
  compiled_cells,
};

struct record {
  record_kind kind;
  const void* bus;
  std::string_view bus_name;
  std::string_view event;
  const void* payload;
  std::string_view payload_name;
  const void* handler;
  std::size_t cells;
  record* next;
};

inline constinit record* records = nullptr;
inline constinit std::atomic<bool> validated{false};
inline constinit std::mutex validation_lock{};
inline char validation_message[1024] = {};

inline int length(std::string_view text) {
  return static_cast<int>(text.size());
}

[[noreturn]] inline void fail(const char* message) noexcept {
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::abort();
}

template <record_kind Kind, class Key, class Handler, std::size_t Cells>
struct recorder {
  record entry;

  recorder() noexcept
    : entry{
        Kind,
        &identity<typename Key::bus>,
        type_name<typename Key::bus>(),
        Key::name.view(),
        &identity<typename Key::payload>,
        type_name<typename Key::payload>(),
        &identity<Handler>,
        Cells,
        nullptr,
      } {
    if (validated.load(std::memory_order_acquire)) {
      std::snprintf(validation_message, sizeof(validation_message),
                    "muypoco: event \"%.*s\" on bus %.*s was registered after the first emit; "
                    "something emitted before main started",
                    length(this->entry.event), this->entry.event.data(),
                    length(this->entry.bus_name), this->entry.bus_name.data());
      fail(validation_message);
    }
    this->entry.next = records;
    records = &this->entry;
  }
};

template <record_kind Kind, class Key, class Handler = void, std::size_t Cells = 0>
inline recorder<Kind, Key, Handler, Cells> recorded{};

inline bool same_event(const record* left, const record* right) {
  return left->bus == right->bus && left->event == right->event;
}

inline bool same_key(const record* left, const record* right) {
  return same_event(left, right) && left->payload == right->payload;
}

inline validation reject(int written) {
  static_cast<void>(written);
  return {false, validation_message};
}

inline validation check_emitted(const record* subscription) {
  for (const record* other = records; other != nullptr; other = other->next) {
    if (other->kind == record_kind::emission && same_event(subscription, other)) {
      return {true, ""};
    }
  }
  return reject(std::snprintf(validation_message, sizeof(validation_message),
                              "muypoco: event \"%.*s\" on bus %.*s has a subscriber with payload %.*s "
                              "but nothing emits it (typo in the event name?)",
                              length(subscription->event), subscription->event.data(),
                              length(subscription->bus_name), subscription->bus_name.data(),
                              length(subscription->payload_name), subscription->payload_name.data()));
}

inline validation check_payloads(const record* current) {
  for (const record* other = current->next; other != nullptr; other = other->next) {
    if (same_event(current, other) && current->payload != other->payload) {
      return reject(std::snprintf(validation_message, sizeof(validation_message),
                                  "muypoco: event \"%.*s\" on bus %.*s is used with two payload types: %.*s and %.*s",
                                  length(current->event), current->event.data(),
                                  length(current->bus_name), current->bus_name.data(),
                                  length(current->payload_name), current->payload_name.data(),
                                  length(other->payload_name), other->payload_name.data()));
    }
  }
  return {true, ""};
}

inline validation check_cells(const record* compiled) {
  std::size_t handlers = 0;

  for (const record* other = records; other != nullptr; other = other->next) {
    if (other->kind == record_kind::static_handler && same_key(compiled, other)) {
      handlers++;
    }
  }

  if (handlers == compiled->cells) {
    return {true, ""};
  }
  return reject(std::snprintf(validation_message, sizeof(validation_message),
                              "muypoco: event \"%.*s\" on bus %.*s has %zu static subscriber(s) in the program "
                              "but emit was compiled with %zu; in the single-file build, a Bus::on inside a template "
                              "that is instantiated after the emit is missed, so move it out of the template",
                              length(compiled->event), compiled->event.data(),
                              length(compiled->bus_name), compiled->bus_name.data(),
                              handlers, compiled->cells));
}

inline validation check_records() {
  for (const record* current = records; current != nullptr; current = current->next) {
    validation result = check_payloads(current);

    if (result.ok && current->kind == record_kind::subscription) {
      result = check_emitted(current);
    }
    if (result.ok && current->kind == record_kind::compiled_cells) {
      result = check_cells(current);
    }
    if (!result.ok) {
      return result;
    }
  }
  return {true, ""};
}

inline validation validate_once() noexcept {
  if (validated.load(std::memory_order_acquire)) {
    return {true, ""};
  }

  std::lock_guard<std::mutex> lock(validation_lock);

  if (validated.load(std::memory_order_relaxed)) {
    return {true, ""};
  }

  const validation result = check_records();

  if (result.ok) {
    validated.store(true, std::memory_order_release);
  }
  return result;
}

inline void validate_or_abort() noexcept {
  const validation result = validate_once();

  if (!result.ok) {
    fail(result.message);
  }
}

template <class Key>
void record_emission() noexcept {
  static_cast<void>(&recorded<record_kind::emission, Key>);
}

template <class Key>
void record_subscription() noexcept {
  static_cast<void>(&recorded<record_kind::subscription, Key>);
}

template <class Key, class Handler>
void record_static_handler() noexcept {
  static_cast<void>(&recorded<record_kind::static_handler, Key, Handler>);
}

template <class Key, std::size_t Cells>
void record_compiled_cells() noexcept {
  static_cast<void>(&recorded<record_kind::compiled_cells, Key, void, Cells>);
}

#else

inline validation validate_once() noexcept {
  return {true, ""};
}

inline void validate_or_abort() noexcept {
}

template <class Key>
void record_emission() noexcept {
}

template <class Key>
void record_subscription() noexcept {
}

template <class Key, class Handler>
void record_static_handler() noexcept {
}

template <class Key, std::size_t Cells>
void record_compiled_cells() noexcept {
}

#endif

}
