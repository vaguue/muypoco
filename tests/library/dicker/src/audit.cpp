#include "../dicker.h"

namespace dicker {

int audited = 0;

void audit() {
  events::on<"ready">([](const Ready&) noexcept {
    audited++;
  });
}

}
