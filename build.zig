const std = @import("std");
const builtin = @import("builtin");

const minimum_zig_version: std.SemanticVersion = .{ .major = 0, .minor = 16, .patch = 0 };

comptime {
  if (builtin.zig_version.order(minimum_zig_version) == .lt) {
    @compileError("muypoco needs zig 0.16.0 or newer, found " ++ builtin.zig_version_string);
  }
}

const Backend = enum {
  registrar,
  @"inline",
};

const cxx_flags = [_][]const u8{
  "-std=c++20",
  "-Wall",
  "-Wextra",
  "-Werror",
  "-fno-exceptions",
  "-fno-rtti",
};

const Config = struct {
  b: *std.Build,
  target: std.Build.ResolvedTarget,
  optimize: std.builtin.OptimizeMode,
  backend: Backend,
  include: std.Build.LazyPath,
};

fn cppModule(config: Config) *std.Build.Module {
  const module = config.b.createModule(.{
    .target = config.target,
    .optimize = config.optimize,
    .link_libcpp = true,
  });

  module.addIncludePath(config.b.path("."));
  module.addIncludePath(config.include);

  if (config.backend == .@"inline") {
    module.addCMacro("MUYPOCO_EVENTS_INLINE", "1");
  }
  if (config.optimize != .Debug) {
    module.addCMacro("NDEBUG", "1");
  }
  return module;
}

fn addSources(module: *std.Build.Module, sources: []const []const u8) void {
  for (sources) |source| {
    module.addCSourceFile(.{ .file = module.owner.path(source), .flags = &cxx_flags });
  }
}

// The registrar backend registers static subscribers from initializers of objects nothing references,
// so a linker pulling members out of a static library drops them. Zig has no whole-archive switch,
// therefore every module that may contain only subscriptions is linked as an object file.
fn addModuleAsObject(config: Config, executable: *std.Build.Step.Compile, name: []const u8, source: []const u8) void {
  const module = cppModule(config);
  addSources(module, &.{source});

  const object = config.b.addObject(.{ .name = name, .root_module = module });
  executable.root_module.addObject(object);
}

fn executableFrom(config: Config, name: []const u8, sources: []const []const u8) *std.Build.Step.Compile {
  const module = cppModule(config);
  addSources(module, sources);
  return config.b.addExecutable(.{ .name = name, .root_module = module });
}

fn unityExecutable(config: Config, name: []const u8, sources: []const []const u8) *std.Build.Step.Compile {
  var content: std.ArrayList(u8) = .empty;

  for (sources) |source| {
    content.print(config.b.allocator, "#include \"{s}\"\n", .{source}) catch @panic("OOM");
  }

  const files = config.b.addWriteFiles();
  const unity = files.add(config.b.fmt("{s}-unity.cpp", .{name}), content.items);
  const module = cppModule(config);

  module.addCSourceFile(.{ .file = unity, .flags = &cxx_flags });
  return config.b.addExecutable(.{ .name = name, .root_module = module });
}

fn modularExecutable(config: Config, name: []const u8, sources: []const []const u8) *std.Build.Step.Compile {
  if (config.backend == .@"inline") {
    return unityExecutable(config, name, sources);
  }

  const executable = executableFrom(config, name, sources[sources.len - 1 ..]);

  for (sources[0 .. sources.len - 1]) |source| {
    addModuleAsObject(config, executable, std.fs.path.stem(source), source);
  }
  return executable;
}

fn backendsToTest(selected: ?Backend) []const Backend {
  if (selected) |backend| {
    return switch (backend) {
      .registrar => &.{.registrar},
      .@"inline" => &.{.@"inline"},
    };
  }
  return &.{ .registrar, .@"inline" };
}

fn addTestRun(b: *std.Build, step: *std.Build.Step, executable: *std.Build.Step.Compile) void {
  const run = b.addRunArtifact(executable);
  run.expectExitCode(0);
  _ = run.captureStdErr(.{});
  step.dependOn(&run.step);
}

fn addAbortTest(config: Config, step: *std.Build.Step, name: []const u8, sources: []const []const u8, message: []const u8) void {
  const run = config.b.addRunArtifact(executableFrom(config, name, sources));
  run.addCheck(.{ .expect_term = .{ .signal = .ABRT } });
  run.addCheck(.{ .expect_stderr_match = message });
  step.dependOn(&run.step);
}

const public_headers = [_][]const u8{
  "events.h",
  "chan.h",
  "timers.h",
  "log.h",
};

fn exportIncludeDirectory(b: *std.Build) std.Build.LazyPath {
  const include = b.addWriteFiles();

  for (public_headers) |header| {
    _ = include.addCopyFile(b.path(header), b.fmt("muypoco/{s}", .{header}));
  }
  _ = include.addCopyDirectory(b.path("detail"), "muypoco/detail", .{ .include_extensions = &.{".h"} });
  b.addNamedLazyPath("include", include.getDirectory());
  return include.getDirectory();
}

pub fn build(b: *std.Build) void {
  const target = b.standardTargetOptions(.{});
  const optimize = b.standardOptimizeOption(.{});

  const include = exportIncludeDirectory(b);
  const selected_backend = b.option(Backend, "backend", "Event emitter backend: registrar (default, standard C++) or inline (single TU, friend injection)");
  const backend = selected_backend orelse .registrar;

  const max_static_ns = b.option([]const u8, "bench-max-static-ns", "Fail bench when a static emit with 4 handlers is slower (ns)") orelse "0";
  const max_runtime_ns = b.option([]const u8, "bench-max-runtime-ns", "Fail bench when a runtime emit with 4 handlers is slower (ns)") orelse "0";
  const max_empty_ns = b.option([]const u8, "bench-max-empty-ns", "Fail bench when an emit without subscribers is slower (ns)") orelse "0";

  const config: Config = .{ .b = b, .target = target, .optimize = optimize, .backend = backend, .include = include };
  const debug: Config = .{ .b = b, .target = target, .optimize = .Debug, .backend = .registrar, .include = include };

  const events_example = [_][]const u8{
    "examples/events/server.cpp",
    "examples/events/audit.cpp",
    "examples/events/stats.cpp",
    "examples/events/main.cpp",
  };
  const single_file_examples = [_][]const u8{
    "examples/await.cpp",
    "examples/chan.cpp",
    "examples/timers.cpp",
    "examples/log.cpp",
    "examples/pipeline.cpp",
    "examples/main_thread.cpp",
  };

  const examples_step = b.step("examples", "Build and run every example (-Dbackend selects the emitter backend)");
  const events_executable = modularExecutable(config, "example-events", &events_example);
  b.installArtifact(events_executable);
  examples_step.dependOn(&b.addRunArtifact(events_executable).step);

  for (single_file_examples) |source| {
    const executable = executableFrom(config, b.fmt("example-{s}", .{std.fs.path.stem(source)}), &.{source});
    b.installArtifact(executable);
    examples_step.dependOn(&b.addRunArtifact(executable).step);
  }

  const test_step = b.step("test", "Run all tests (both backends unless -Dbackend is given)");

  addTestRun(b, test_step, executableFrom(config, "cells-pinning", &.{"tests/cells_test.cpp"}));
  addTestRun(b, test_step, executableFrom(config, "chan", &.{"tests/chan_test.cpp"}));
  addTestRun(b, test_step, executableFrom(config, "timers", &.{"tests/timers_test.cpp"}));
  addTestRun(b, test_step, executableFrom(config, "log", &.{"tests/log_test.cpp"}));

  for (single_file_examples) |source| {
    addTestRun(b, test_step, executableFrom(config, b.fmt("example-{s}-test", .{std.fs.path.stem(source)}), &.{source}));
  }

  for (backendsToTest(selected_backend)) |tested| {
    const tested_config: Config = .{ .b = b, .target = target, .optimize = optimize, .backend = tested, .include = include };
    addTestRun(b, test_step, executableFrom(tested_config, b.fmt("events-{s}", .{@tagName(tested)}), &.{"tests/events_test.cpp"}));
    addTestRun(b, test_step, modularExecutable(tested_config, b.fmt("example-events-{s}", .{@tagName(tested)}), &events_example));
    addTestRun(b, test_step, executableFrom(tested_config, b.fmt("example-pipeline-{s}", .{@tagName(tested)}), &.{"examples/pipeline.cpp"}));
  }

  const registrar: Config = .{ .b = b, .target = target, .optimize = optimize, .backend = .registrar, .include = include };
  const emitter_module = cppModule(registrar);
  addSources(emitter_module, &.{"tests/cross_tu/emitter.cpp"});
  const emitter_library = b.addLibrary(.{ .name = "cross-emitter", .root_module = emitter_module });

  const cross = executableFrom(registrar, "cross-tu", &.{"tests/cross_tu/main.cpp"});
  cross.root_module.linkLibrary(emitter_library);
  addModuleAsObject(registrar, cross, "subscriber_a", "tests/cross_tu/subscriber_a.cpp");
  addModuleAsObject(registrar, cross, "subscriber_b", "tests/cross_tu/subscriber_b.cpp");
  addTestRun(b, test_step, cross);

  addTestRun(b, test_step, executableFrom(debug, "validation-report", &.{"tests/validation/report.cpp"}));
  addAbortTest(debug, test_step, "validation-typo", &.{"tests/validation/typo.cpp"}, "\"dta\" on bus muypoco::events has a subscriber with payload Data but nothing emits it");
  addAbortTest(debug, test_step, "validation-mismatch", &.{"tests/validation/mismatch.cpp"}, "\"data\" on bus muypoco::events is used with two payload types");
  addAbortTest(debug, test_step, "validation-early-emit", &.{ "tests/validation/early_emit_emitter.cpp", "tests/validation/early_emit_subscriber.cpp" }, "something emitted before main started");

  addAbortTest(debug, test_step, "chan-second-producer", &.{"tests/checks/second_producer.cpp"}, "Chan is single-producer but a second thread enqueued");
  addAbortTest(debug, test_step, "chan-self-enqueue", &.{"tests/checks/self_enqueue.cpp"}, "Chan consumer enqueued into its own channel");
  addAbortTest(debug, test_step, "chan-second-consumer", &.{"tests/checks/second_consumer.cpp"}, "Chan is single-consumer but a second thread consumed it");

  const debug_inline: Config = .{ .b = b, .target = target, .optimize = .Debug, .backend = .@"inline", .include = include };
  addAbortTest(debug_inline, test_step, "validation-split-inline", &.{ "tests/validation/split_emitter.cpp", "tests/validation/split_subscriber.cpp" }, "the inline backend needs a single translation unit");

  const bench_step = b.step("bench", "Benchmark emit on every path (always ReleaseFast); -Dbench-max-*-ns fail on regression");

  for ([_]Backend{ .registrar, .@"inline" }) |benched| {
    const bench_config: Config = .{ .b = b, .target = target, .optimize = .ReleaseFast, .backend = benched, .include = include };
    const bench = b.addRunArtifact(executableFrom(bench_config, b.fmt("bench-{s}", .{@tagName(benched)}), &.{"bench/emit_bench.cpp"}));
    bench.addArgs(&.{ "--max-static-ns", max_static_ns, "--max-runtime-ns", max_runtime_ns, "--max-empty-ns", max_empty_ns });
    bench.has_side_effects = true;
    bench_step.dependOn(&bench.step);
  }
}
