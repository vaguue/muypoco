#pragma once

#include <cstdio>
#include <source_location>

namespace testing {

inline int failures = 0;
inline int checks = 0;

inline void check(bool condition, const char* what, std::source_location where = std::source_location::current()) {
  checks++;
  if (condition) {
    return;
  }
  failures++;
  std::fprintf(stderr, "%s:%u: check failed: %s\n", where.file_name(), static_cast<unsigned>(where.line()), what);
}

inline int finish(const char* suite) {
  std::printf("%s: %d checks, %d failures\n", suite, checks, failures);
  return failures == 0 ? 0 : 1;
}

}
