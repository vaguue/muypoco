#pragma once

#include <cstddef>
#include <string_view>

namespace muypoco {

/// String literal usable as a template argument: the event name in `on<"data">`.
template <std::size_t Size>
struct fixed_string {
  char chars[Size] = {};

  constexpr fixed_string(const char (&text)[Size]) {
    for (std::size_t i = 0; i < Size; i++) {
      this->chars[i] = text[i];
    }
  }

  constexpr std::string_view view() const {
    return std::string_view(this->chars, Size - 1);
  }
};

}
