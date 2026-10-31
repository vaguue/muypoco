#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "muypoco/chan.h"

struct Write {
  std::string key;
  std::string value;
  std::shared_ptr<muypoco::Completion> completion;
};

struct Store {
  std::vector<std::string> rows;

  bool init() noexcept {
    std::printf("store: connected\n");
    return true;
  }

  bool process(Write& write) noexcept {
    if (write.key.empty()) {
      return false;
    }
    this->rows.push_back(write.key + "=" + write.value);
    return true;
  }
};

int main() {
  muypoco::Chan<Store, Write, 1024> store;
  store.start();

  if (!store.awaitInit()) {
    return 1;
  }

  for (int i = 0; i < 3; i++) {
    store.enqueue(Write{"user" + std::to_string(i), "online", nullptr});
  }

  const bool ok = store.await(Write{"session", "42", nullptr});
  std::printf("await: %s, queued: %u\n", ok ? "ok" : "failed", store.occupancy());

  if (!store.tryEnqueue(Write{"metrics", "1", nullptr})) {
    std::printf("full, dropping metrics\n");
  }

  store.shutdown();
  std::printf("store: %zu rows\n", store.worker.rows.size());
  return 0;
}
