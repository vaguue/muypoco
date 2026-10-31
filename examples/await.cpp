#include <coroutine>
#include <cstdio>

#include "muypoco/events.h"

struct Config {
  int port;
};

struct Database {
  const char* url;
};

struct detached {
  struct promise_type {
    detached get_return_object() noexcept {
      return {};
    }

    std::suspend_never initial_suspend() noexcept {
      return {};
    }

    std::suspend_never final_suspend() noexcept {
      return {};
    }

    void return_void() noexcept {
    }

    void unhandled_exception() noexcept {
    }
  };
};

detached boot() {
  const Config config = co_await muypoco::events.once<"config.loaded", Config>();
  std::printf("config: port %d\n", config.port);

  const Database database = co_await muypoco::events.once<"db.ready", Database>();
  std::printf("db: %s\n", database.url);

  std::printf("listening on %d\n", config.port);
}

int main() {
  boot();
  muypoco::events.emit<"config.loaded">(Config{8080});
  muypoco::events.emit<"db.ready">(Database{"postgres://localhost/app"});
  return 0;
}
