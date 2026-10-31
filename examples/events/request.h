#pragma once

#include <cstdint>

struct request {
  std::uint64_t id;
  std::uint32_t bytes;
};
