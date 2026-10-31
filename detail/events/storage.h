#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace muypoco::detail {

template <class Payload>
struct entry {
  void (*call)(void* context, const Payload& payload) noexcept = nullptr;
  void* context = nullptr;
};

template <class Payload>
struct segment {
  static constexpr std::size_t capacity = 16;

  entry<Payload> entries[capacity] = {};
  segment* next = nullptr;
};

inline constinit std::mutex subscription_lock{};

template <class Payload>
struct table {
  segment<Payload> head{};
  segment<Payload>* tail = nullptr;
  std::atomic<std::size_t> count{0};

  void append(entry<Payload> subscriber) noexcept {
    std::lock_guard<std::mutex> lock(subscription_lock);

    if (this->tail == nullptr) {
      this->tail = &this->head;
    }

    const std::size_t index = this->count.load(std::memory_order_relaxed);
    const std::size_t slot = index % segment<Payload>::capacity;

    if (index > 0 && slot == 0) {
      this->tail->next = new segment<Payload>{};
      this->tail = this->tail->next;
    }

    this->tail->entries[slot] = subscriber;
    this->count.store(index + 1, std::memory_order_release);
  }

  void emit(const Payload& payload) const noexcept {
    std::size_t remaining = this->count.load(std::memory_order_acquire);
    const segment<Payload>* current = &this->head;

    while (remaining > 0) {
      const std::size_t batch = std::min(remaining, segment<Payload>::capacity);

      for (std::size_t i = 0; i < batch; i++) {
        current->entries[i].call(current->entries[i].context, payload);
      }

      remaining -= batch;
      current = current->next;
    }
  }
};

template <class Key>
inline constinit table<typename Key::payload> storage{};

}
