#include "cross.h"

#include "../check.h"

using testing::check;

int main() {
  emit_ping(1);
  check(hits_a == 1, "subscriber in another object file fires");
  check(hits_b == 1, "second subscriber in another object file fires");
  check(hits_shared == 1, "subscriber in a header included by two TUs registers exactly once");
  return testing::finish("cross-TU (registrar backend)");
}
