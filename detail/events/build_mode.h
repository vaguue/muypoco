#pragma once

#include <cstdio>

#include "../check.h"

namespace muypoco::detail {

enum struct build_mode {
  normal,
  single_file,
};

inline const char* describe(build_mode mode) {
  if (mode == build_mode::normal) {
    return "the normal build";
  }
  return "the single-file build";
}

struct first_build_mode {
  const char* file = nullptr;
  build_mode mode = build_mode::normal;
};

inline constinit first_build_mode first_seen{};
inline char build_mode_message[1024] = {};

struct build_mode_claim {
  build_mode_claim(build_mode mode, const char* file) noexcept {
    if (first_seen.file == nullptr) {
      first_seen = {file, mode};
      return;
    }
    if (first_seen.mode == mode) {
      return;
    }
    std::snprintf(build_mode_message, sizeof(build_mode_message),
                  "muypoco: mixed builds: %s uses %s but %s uses %s; "
                  "compile every file that uses muypoco events the same way",
                  file, describe(mode), first_seen.file, describe(first_seen.mode));
    violated(build_mode_message);
  }
};

namespace {

#ifdef MUYPOCO_EVENTS_INLINE
constexpr build_mode translation_unit_mode = build_mode::single_file;
#else
constexpr build_mode translation_unit_mode = build_mode::normal;
#endif

#ifdef __BASE_FILE__
constexpr const char* translation_unit_file = __BASE_FILE__;
#else
constexpr const char* translation_unit_file = __FILE__;
#endif

const build_mode_claim translation_unit_claim{translation_unit_mode, translation_unit_file};

}
}
