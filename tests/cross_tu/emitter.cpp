#include "cross.h"

void emit_ping(int value) {
  cross.emit<"ping">(ping{value});
}
