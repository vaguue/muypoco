#pragma once

#include "fixed_string.h"

namespace muypoco::detail {

template <class Bus, fixed_string Name, class Payload>
struct key {
  using bus = Bus;
  using payload = Payload;

  static constexpr auto name = Name;
};

}
