#pragma once

#include <cstdio>
#include <cstdlib>

namespace muypoco::detail {

#ifdef NDEBUG
inline constexpr bool checks = false;
#else
inline constexpr bool checks = true;
#endif

[[noreturn]] inline void violated(const char* message) noexcept {
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::abort();
}

inline void check(bool condition, const char* message) noexcept {
  if constexpr (checks) {
    if (!condition) {
      violated(message);
    }
  }
}

}
