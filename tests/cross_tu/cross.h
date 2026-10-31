#pragma once

#include "events.h"

struct ping {
  int value;
};

struct cross : muypoco::bus<cross> {};
inline constexpr muypoco::handle<cross> cross{};

inline int hits_a = 0;
inline int hits_b = 0;
inline int hits_shared = 0;

void emit_ping(int value);
