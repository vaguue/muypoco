#include "log.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "check.h"

using testing::check;

std::string read_all(const char* path) {
  std::string content;
  std::FILE* file = std::fopen(path, "rb");

  if (file == nullptr) {
    return content;
  }

  char chunk[4096];
  std::size_t got = 0;

  while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
    content.append(chunk, got);
  }
  std::fclose(file);
  return content;
}

int open_log(const char* path) {
  return ::open(path, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0644);
}

void test_direct(const char* path) {
  muypoco::Logger logger;
  const int fd = open_log(path);

  logger.output(fd);
  logger.configure(muypoco::level::info);
  logger.debug("hidden %d", 1);
  logger.info("visible %d", 2);
  logger.warn("careful %s", "here");
  logger.error("broken");
  ::close(fd);

  check(read_all(path) == "[*] visible 2\n[?] careful here\n[!] broken\n", "direct mode writes formatted lines above the threshold");
}

void test_truncation(const char* path) {
  muypoco::Logger logger;
  const int fd = open_log(path);
  const std::string huge(5000, 'x');

  logger.output(fd);
  logger.info("%s", huge.c_str());
  ::close(fd);

  const std::string content = read_all(path);
  check(content.size() == muypoco::detail::logging::line_capacity - 1, "a long line is truncated to the line buffer");
  check(!content.empty() && content.back() == '\n', "a truncated line still ends with a newline");
}

void test_background(const char* path) {
  constexpr int threads = 4;
  constexpr int lines = 2000;
  muypoco::Logger logger;
  const int fd = open_log(path);

  logger.output(fd);
  logger.start();

  std::vector<std::thread> workers;

  for (int t = 0; t < threads; t++) {
    workers.emplace_back([&logger, t] {
      for (int i = 0; i < lines; i++) {
        logger.info("thread %d line %d", t, i);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }

  logger.stop();
  ::close(fd);

  const std::string content = read_all(path);
  std::size_t written = 0;
  bool intact = true;
  std::size_t start = 0;

  while (start < content.size()) {
    const std::size_t end = content.find('\n', start);
    const std::string line = content.substr(start, end - start);
    int thread = -1;
    int index = -1;

    if (std::sscanf(line.c_str(), "[*] thread %d line %d", &thread, &index) != 2) {
      intact = false;
    }
    written++;
    start = end + 1;
  }

  check(written + logger.dropped() == threads * lines, "every line is either written or counted as dropped");
  check(written > 0, "background mode writes lines");
  check(intact, "lines from different threads never interleave");
}

int main() {
  const char* path = "muypoco-log-test.txt";

  test_direct(path);
  test_truncation(path);
  test_background(path);
  std::remove(path);
  return testing::finish("log");
}
