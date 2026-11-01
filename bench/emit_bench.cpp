#include "events.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

struct tick {
  std::uint64_t value;
};

MUYPOCO_BUS(bench);

volatile std::uint64_t sink = 0;

void static_subscribers() {
  bench::on<"static">([](const tick& payload) noexcept {
    sink = sink + payload.value;
  });
  bench::on<"static">([](const tick& payload) noexcept {
    sink = sink + payload.value * 2;
  });
  bench::on<"static">([](const tick& payload) noexcept {
    sink = sink + payload.value * 3;
  });
  bench::on<"static">([](const tick& payload) noexcept {
    sink = sink + payload.value * 4;
  });
}

void by_hand(const tick& payload) {
  sink = sink + payload.value;
  sink = sink + payload.value * 2;
  sink = sink + payload.value * 3;
  sink = sink + payload.value * 4;
}

std::vector<std::function<void(const tick&)>> functions;

void function_subscribers() {
  for (std::uint64_t factor = 1; factor <= 4; factor++) {
    functions.push_back([factor](const tick& payload) {
      sink = sink + payload.value * factor;
    });
  }
}

void runtime_subscribers() {
  for (std::uint64_t factor = 1; factor <= 4; factor++) {
    bench.on<"runtime">([factor](const tick& payload) noexcept {
      sink = sink + payload.value * factor;
    });
  }
}

struct limits {
  double static_ns = 0;
  double runtime_ns = 0;
  double empty_ns = 0;
};

limits parse(int count, char** arguments) {
  limits result;

  for (int i = 1; i + 1 < count; i += 2) {
    const double value = std::strtod(arguments[i + 1], nullptr);

    if (std::strcmp(arguments[i], "--max-static-ns") == 0) {
      result.static_ns = value;
    }
    else if (std::strcmp(arguments[i], "--max-runtime-ns") == 0) {
      result.runtime_ns = value;
    }
    else if (std::strcmp(arguments[i], "--max-empty-ns") == 0) {
      result.empty_ns = value;
    }
  }
  return result;
}

template <class Emit>
double measure(Emit emit) {
  constexpr std::uint64_t warmup = 1'000'000;
  constexpr std::uint64_t iterations = 50'000'000;

  for (std::uint64_t i = 0; i < warmup; i++) {
    emit(tick{i});
  }

  const auto start = std::chrono::steady_clock::now();

  for (std::uint64_t i = 0; i < iterations; i++) {
    emit(tick{i});
  }

  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration<double, std::nano>(elapsed).count() / static_cast<double>(iterations);
}

bool report(const char* path, double nanoseconds, double limit) {
  const bool ok = limit <= 0 || nanoseconds <= limit;

  if (limit > 0) {
    std::printf("%-32s %7.3f ns/emit  (limit %.3f)  %s\n", path, nanoseconds, limit, ok ? "ok" : "REGRESSION");
  }
  else {
    std::printf("%-32s %7.3f ns/emit\n", path, nanoseconds);
  }
  return ok;
}

int main(int count, char** arguments) {
  const limits limit = parse(count, arguments);

  runtime_subscribers();
  function_subscribers();

#ifdef MUYPOCO_EVENTS_INLINE
  const char* static_path = "inline, 4 handlers";
#else
  const char* static_path = "registrar, 4 handlers";
#endif

  const double static_ns = measure([](const tick& payload) {
    bench.emit<"static">(payload);
  });
  const double runtime_ns = measure([](const tick& payload) {
    bench.emit<"runtime">(payload);
  });
  const double empty_ns = measure([](const tick& payload) {
    bench.emit<"nobody">(payload);
  });

  const double by_hand_ns = measure([](const tick& payload) {
    by_hand(payload);
  });
  const double functions_ns = measure([](const tick& payload) {
    for (const std::function<void(const tick&)>& function : functions) {
      function(payload);
    }
  });

  report("baseline: 4 calls by hand", by_hand_ns, 0);
  report("baseline: vector<std::function>", functions_ns, 0);

  bool ok = true;
  ok = report(static_path, static_ns, limit.static_ns) && ok;
  ok = report("runtime, 4 handlers", runtime_ns, limit.runtime_ns) && ok;
  ok = report("no subscribers", empty_ns, limit.empty_ns) && ok;
  return ok ? 0 : 1;
}
