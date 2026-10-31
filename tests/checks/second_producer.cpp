#include "chan.h"

#include <thread>

struct Plain {
  int value = 0;
};

struct Sink {
  void process(Plain&) noexcept {
  }
};

int main() {
  muypoco::Chan<Sink, Plain, 16> chan;
  chan.start();
  chan.enqueue(Plain{});

  std::thread other([&chan] {
    chan.enqueue(Plain{});
  });
  other.join();
  return 0;
}
