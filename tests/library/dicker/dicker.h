#pragma once

#include "muypoco/events.h"

namespace dicker {

MUYPOCO_BUS(events);

struct Ready {
  int connections;
};

extern int audited;

void start();

}
