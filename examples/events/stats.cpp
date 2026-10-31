#include <cstdio>

#include "muypoco/events.h"
#include "request.h"

struct stats {
  std::uint64_t requests = 0;
  std::uint64_t bytes = 0;

  void print() const {
    std::printf("stats: %llu requests, %llu bytes\n",
                static_cast<unsigned long long>(this->requests),
                static_cast<unsigned long long>(this->bytes));
  }
};

stats& start_stats() {
  static stats collected;

  muypoco::events.on<"request">([&collected = collected](const request& incoming) noexcept {
    collected.requests++;
    collected.bytes += incoming.bytes;
  });
  return collected;
}

void print_stats(const stats& collected) {
  collected.print();
}
