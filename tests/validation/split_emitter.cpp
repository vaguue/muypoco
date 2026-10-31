#include "events.h"

struct Data {
  int value;
};

int main() {
  muypoco::events.emit<"split">(Data{1});
  return 0;
}
