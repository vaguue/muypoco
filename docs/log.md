# Log {#log}

`#include "muypoco/log.h"`

A printf-style logger that stays out of the hot path.

```cpp
muypoco::log().info("listening on %d", port);
muypoco::log().error("connect failed: %s", reason);
```

| call | prefix |
|---|---|
| `debug` | `[.]` |
| `info` | `[*]` |
| `warn` | `[?]` |
| `error` | `[!]` |

The compiler checks format strings.

## Level and output

```cpp
muypoco::log().configure(muypoco::level::warn);   // debug, info, warn, error, off
muypoco::log().output(fd);                        // stderr by default
```

## Direct mode (default)

Each line is formatted on the stack and written with a single `write`. Nothing is locked, and lines from different threads never interleave. Each line still costs one system call.

## Background mode

```cpp
muypoco::log().start();
// ...
muypoco::log().stop();   // writes what's buffered; also runs at exit
```

Each thread logs into its own 64 KB ring buffer, and one background thread writes all of them in batches. The logging thread only pays for formatting. If a ring fills up, the line is dropped instead of blocking:

```cpp
muypoco::log().dropped();
```

Each thread's lines stay in order. Lines from different threads can come out in a different order than they happened, because each thread's buffer is written as a batch.

Lines are up to 1 KB. Longer ones are truncated but keep their newline.

## Own instance

```cpp
muypoco::Logger audit;
audit.output(audit_fd);
audit.start();
```

`log.h` uses POSIX `write`.
