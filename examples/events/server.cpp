#include "muypoco/events.h"
#include "request.h"

void serve(std::uint64_t count) {
  for (std::uint64_t id = 1; id <= count; id++) {
    muypoco::events.emit<"request">(request{id, static_cast<std::uint32_t>(id * 100)});
  }
}
