#include "events.h"

struct Data {
  int value;
};

struct emits_at_startup {
  emits_at_startup() {
    muypoco::events.emit<"startup">(Data{1});
  }
};

emits_at_startup instance;

int main() {
  return 0;
}
