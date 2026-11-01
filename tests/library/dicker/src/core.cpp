#include "../dicker.h"

namespace dicker {

void start() {
  events.emit<"ready">(Ready{3});
}

}
