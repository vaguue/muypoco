#include "chan.h"

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
  chan.drain();
  return 0;
}
