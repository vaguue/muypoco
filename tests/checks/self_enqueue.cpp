#include "chan.h"

struct Plain {
  int value = 0;
};

struct Echo;

using EchoChan = muypoco::Chan<Echo, Plain, 16>;

struct Echo {
  EchoChan* self = nullptr;

  void process(Plain& task) noexcept {
    if (task.value == 1) {
      this->self->enqueue(Plain{0});
    }
  }
};

int main() {
  EchoChan chan;
  chan.worker.self = &chan;
  chan.start();
  chan.enqueue(Plain{1});
  chan.shutdown();
  return 0;
}
