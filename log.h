#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

/// @file log.h
/// printf-style logger: one write per line, no locks on the logging path, optional background writer.

namespace muypoco {

/// Log levels, lowest first.
enum struct level {
  debug,
  info,
  warn,
  error,
  off,
};

namespace detail::logging {

inline constexpr std::size_t line_capacity = 1024;
inline constexpr std::size_t ring_capacity = std::size_t{1} << 16;

struct ring {
  char bytes[ring_capacity];
  alignas(64) std::atomic<std::size_t> head{0};
  alignas(64) std::atomic<std::size_t> tail{0};
  std::atomic<bool> orphaned{false};

  bool push(const char* text, std::size_t size) noexcept {
    const std::size_t written = this->head.load(std::memory_order_relaxed);

    if (ring_capacity - (written - this->tail.load(std::memory_order_acquire)) < size) {
      return false;
    }

    const std::size_t offset = written % ring_capacity;
    const std::size_t first = std::min(size, ring_capacity - offset);

    std::memcpy(this->bytes + offset, text, first);
    std::memcpy(this->bytes, text + first, size - first);
    this->head.store(written + size, std::memory_order_release);
    return true;
  }
};

struct ring_owner {
  ring* owned = nullptr;

  ~ring_owner() {
    if (this->owned != nullptr) {
      this->owned->orphaned.store(true, std::memory_order_release);
    }
  }
};

inline thread_local ring_owner local_ring;

inline bool write_all(int descriptor, const char* text, std::size_t size) noexcept {
  while (size > 0) {
    const ssize_t written = ::write(descriptor, text, size);

    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }

    text += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

}

/// The logger behind muypoco::log().
///
/// By default every line is formatted on the stack and written with a single `write`,
/// so lines from different threads never interleave and nothing is locked.
/// After start(), lines go to a per-thread ring buffer instead and a background thread writes them in batches;
/// the logging thread only pays for formatting. When a ring is full the line is dropped and counted.
struct Logger {
  std::atomic<level> threshold{level::info};
  std::atomic<int> descriptor{STDERR_FILENO};
  std::atomic<bool> running{false};
  std::atomic<std::size_t> drops{0};
  std::mutex rings_lock;
  std::vector<detail::logging::ring*> rings;
  std::vector<char> batch;
  std::thread writer;

  /// Lines below `minimum` are skipped. level::off silences everything.
  void configure(level minimum) {
    this->threshold.store(minimum, std::memory_order_relaxed);
  }

  /// Where lines go, stderr by default.
  void output(int fd) {
    this->descriptor.store(fd, std::memory_order_relaxed);
  }

  /// Whether a line at `wanted` would be written.
  bool enabled(level wanted) const {
    return wanted >= this->threshold.load(std::memory_order_relaxed);
  }

  /// Lines dropped because a thread's ring buffer was full.
  std::size_t dropped() const {
    return this->drops.load(std::memory_order_relaxed);
  }

  void debug(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list arguments;
    va_start(arguments, format);
    this->print(level::debug, "[.] ", format, arguments);
    va_end(arguments);
  }

  void info(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list arguments;
    va_start(arguments, format);
    this->print(level::info, "[*] ", format, arguments);
    va_end(arguments);
  }

  void warn(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list arguments;
    va_start(arguments, format);
    this->print(level::warn, "[?] ", format, arguments);
    va_end(arguments);
  }

  void error(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    va_list arguments;
    va_start(arguments, format);
    this->print(level::error, "[!] ", format, arguments);
    va_end(arguments);
  }

  /// Switches to background writing. Lines logged before stop() may be up to about a millisecond late.
  void start() {
    std::lock_guard<std::mutex> lock(this->rings_lock);

    if (this->running.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    this->writer = std::thread([this] {
      this->run();
    });
  }

  /// Writes everything still buffered and goes back to direct writes.
  void stop() {
    if (!this->running.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    if (this->writer.joinable()) {
      this->writer.join();
    }
    this->drain();
  }

  ~Logger() {
    this->stop();
  }

  void print(level wanted, const char* prefix, const char* format, va_list arguments) noexcept {
    if (!this->enabled(wanted)) {
      return;
    }

    char line[detail::logging::line_capacity];
    std::size_t size = std::strlen(prefix);
    std::memcpy(line, prefix, size);

    const int formatted = std::vsnprintf(line + size, sizeof(line) - size - 1, format, arguments);

    if (formatted < 0) {
      return;
    }

    size += std::min(static_cast<std::size_t>(formatted), sizeof(line) - size - 2);
    line[size] = '\n';
    size++;

    if (this->running.load(std::memory_order_acquire)) {
      if (!this->local_ring()->push(line, size)) {
        this->drops.fetch_add(1, std::memory_order_relaxed);
      }
      return;
    }
    detail::logging::write_all(this->descriptor.load(std::memory_order_relaxed), line, size);
  }

  detail::logging::ring* local_ring() {
    detail::logging::ring_owner& owner = detail::logging::local_ring;

    if (owner.owned == nullptr) {
      owner.owned = new detail::logging::ring;
      std::lock_guard<std::mutex> lock(this->rings_lock);
      this->rings.push_back(owner.owned);
    }
    return owner.owned;
  }

  std::size_t drain() {
    std::lock_guard<std::mutex> lock(this->rings_lock);

    this->batch.clear();

    for (std::size_t i = 0; i < this->rings.size();) {
      detail::logging::ring* current = this->rings[i];
      const bool orphaned = current->orphaned.load(std::memory_order_acquire);
      const std::size_t head = current->head.load(std::memory_order_acquire);
      const std::size_t tail = current->tail.load(std::memory_order_relaxed);

      for (std::size_t position = tail; position < head;) {
        const std::size_t offset = position % detail::logging::ring_capacity;
        const std::size_t chunk = std::min(head - position, detail::logging::ring_capacity - offset);

        this->batch.insert(this->batch.end(), current->bytes + offset, current->bytes + offset + chunk);
        position += chunk;
      }
      current->tail.store(head, std::memory_order_release);

      if (orphaned) {
        delete current;
        this->rings[i] = this->rings.back();
        this->rings.pop_back();
      }
      else {
        i++;
      }
    }

    detail::logging::write_all(this->descriptor.load(std::memory_order_relaxed), this->batch.data(), this->batch.size());
    return this->batch.size();
  }

  void run() {
    while (this->running.load(std::memory_order_acquire)) {
      if (this->drain() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }
};

/// The process-wide logger: `muypoco::log().info("listening on %d", port);`
inline Logger& log() {
  static Logger instance;
  return instance;
}

}
