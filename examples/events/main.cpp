#include <cstdint>
#include <cstdio>

#include "muypoco/events.h"

struct stats;

void serve(std::uint64_t count);
stats& start_stats();
void print_stats(const stats& collected);

int main() {
  const auto [ok, message] = muypoco::validate();

  if (!ok) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
  }

  const stats& collected = start_stats();
  serve(4);
  print_stats(collected);
  return 0;
}
