#include <thread>
#include <vector>

#include "muypoco/log.h"

int main() {
  muypoco::Logger& log = muypoco::log();

  log.configure(muypoco::level::debug);
  log.debug("direct mode: one write per line, nothing locked");

  log.start();

  std::vector<std::thread> workers;

  for (int id = 0; id < 4; id++) {
    workers.emplace_back([id] {
      for (int request = 0; request < 3; request++) {
        muypoco::log().info("worker %d handled request %d", id, request);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }

  log.stop();
  log.configure(muypoco::level::warn);
  log.info("filtered out");
  log.warn("dropped lines: %zu", log.dropped());
  return 0;
}
