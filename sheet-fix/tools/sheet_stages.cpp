// sheet-stages: replays inputs and reports where each one stops.
//
//   sheet-stages [--csv] [--timeout SECONDS] [--verbose] DIR_OR_FILE...
//
// Prints a histogram of the stage that rejected each input ("done" means it
// passed every stage; "empty" means it passed but defines no cells, so no
// stage did any real work). Each input runs in a forked child, so an input
// that triggers a bug is counted as "crash" (or "hang", if it runs longer
// than the timeout, default 5 seconds) instead of stopping the tool. The
// children's error output is hidden unless --verbose is given; replay a
// crashing input with sheet-repro to see its report.
//
// With --csv, prints one row per input instead:
// file,stage,line,column,message.
//
// Source coverage: in the coverage build, each child appends its profile to
// LLVM_PROFILE_FILE (the runtime truncates the file once, when the tool
// starts), and llvm-profdata merge combines them. A crashing child adds no
// profile.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pipeline.hpp"
#include "sheet/print.hpp"

// Provided by the profiling runtime in the coverage build only.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): fixed runtime name.
extern "C" __attribute__((weak)) auto __llvm_profile_write_file() -> int;

namespace {

struct Options {
  bool csv = false;
  bool verbose = false;
  unsigned timeout_seconds = 5;
  std::vector<std::filesystem::path> paths;
};

// Written by the child, read by the parent.
struct SharedResult {
  static constexpr std::uint8_t unknown = 0xFF;  // the child died before finishing
  static constexpr std::uint8_t empty = 0xFE;    // accepted, but defines no cells
  std::uint8_t stage = unknown;
  std::uint32_t line = 0;
  std::uint32_t column = 0;
  std::array<char, 512> message{};
};

// How one input ended, from the parent's point of view.
struct Result {
  std::string label;  // a stage name, "empty", "crash" or "hang"
  std::uint32_t line = 0;
  std::uint32_t column = 0;
  std::string message;
};

auto csv_escape(std::string_view text) -> std::string {
  std::string escaped{"\""};
  for (const char c : text) {
    if (c == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(c == '\n' ? ' ' : c);
  }
  escaped.push_back('"');
  return escaped;
}

auto print_usage() -> void {
  sheet::println(stderr, "usage: sheet-stages [--csv] [--timeout SECONDS] [--verbose] DIR_OR_FILE...");
}

auto parse_options(std::span<char*> args) -> std::optional<Options> {
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--csv") {
      options.csv = true;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--timeout") {
      if (i + 1 >= args.size()) {
        return std::nullopt;
      }
      const std::string_view value = args[++i];
      unsigned seconds = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seconds);
      if (error != std::errc{} || end != value.data() + value.size() || seconds == 0) {
        return std::nullopt;
      }
      options.timeout_seconds = seconds;
    } else if (arg.starts_with("-")) {
      return std::nullopt;
    } else {
      options.paths.emplace_back(arg);
    }
  }
  if (options.paths.empty()) {
    return std::nullopt;
  }
  return options;
}

auto silence_stderr() -> void {
  const int null = open("/dev/null", O_WRONLY);
  if (null >= 0) {
    dup2(null, STDERR_FILENO);
    close(null);
  }
}

// Runs the pipeline on one input in a child process.
auto run_child(const std::string& source, SharedResult& shared, const Options& options) -> Result {
  shared = SharedResult{};
  std::fflush(stdout);
  std::fflush(stderr);
  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("sheet-stages: fork");
    std::exit(1);
  }
  if (pid == 0) {
    if (!options.verbose) {
      silence_stderr();
    }
    alarm(options.timeout_seconds);
    const auto outcome = sheet::tools::run_pipeline(source);
    if (const auto& d = outcome.diagnostic) {
      shared.line = static_cast<std::uint32_t>(d->location.line);
      shared.column = static_cast<std::uint32_t>(d->location.column);
      const auto length = std::min(d->message.size(), shared.message.size() - 1);
      std::copy_n(d->message.begin(), length, shared.message.begin());
    }
    shared.stage = sheet::tools::is_empty_success(outcome) ? SharedResult::empty
                                                           : static_cast<std::uint8_t>(outcome.stage);
    if (__llvm_profile_write_file != nullptr) {
      __llvm_profile_write_file();
    }
    _exit(0);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
  }
  if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
    return Result{.label = "hang",
                  .message = std::format("still running after {} s", options.timeout_seconds)};
  }
  if (WIFSIGNALED(status)) {
    return Result{.label = "crash", .message = std::format("signal {}", WTERMSIG(status))};
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || shared.stage == SharedResult::unknown) {
    return Result{.label = "crash", .message = std::format("exit {}", WEXITSTATUS(status))};
  }
  const std::string_view stage = shared.stage == SharedResult::empty
                                     ? std::string_view{"empty"}
                                     : to_string(static_cast<sheet::Stage>(shared.stage));
  return Result{.label = std::string{stage},
                .line = shared.line,
                .column = shared.column,
                .message = std::string{shared.message.data()}};
}

auto run(std::span<char*> args) -> int {
  for (const std::string_view arg : args) {
    if (arg == "-h" || arg == "--help") {
      print_usage();
      return 0;
    }
  }
  const auto options = parse_options(args);
  if (!options) {
    print_usage();
    return 2;
  }

  void* memory = mmap(nullptr, sizeof(SharedResult), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (memory == MAP_FAILED) {
    std::perror("sheet-stages: mmap");
    return 1;
  }
  auto& shared = *static_cast<SharedResult*>(memory);

  const auto inputs = sheet::tools::collect_inputs(options->paths);
  std::array<std::size_t, sheet::tools::all_stages.size()> counts{};
  std::size_t empty = 0;
  std::size_t crashes = 0;
  std::size_t hangs = 0;

  if (options->csv) {
    sheet::println("file,stage,line,column,message");
  }
  for (const auto& input : inputs) {
    const auto source = sheet::tools::read_file(input);
    if (!source) {
      sheet::println(stderr, "warning: cannot read {}", input.string());
      continue;
    }
    const auto result = run_child(*source, shared, *options);
    if (result.label == "crash") {
      ++crashes;
    } else if (result.label == "hang") {
      ++hangs;
    } else if (result.label == "empty") {
      ++empty;
    } else {
      for (const auto stage : sheet::tools::all_stages) {
        if (result.label == to_string(stage)) {
          ++counts[static_cast<std::size_t>(stage)];
        }
      }
    }
    if (options->csv) {
      sheet::println("{},{},{},{},{}", csv_escape(input.string()), result.label, result.line,
                     result.column, csv_escape(result.message));
    }
  }

  if (!options->csv) {
    std::size_t total = empty + crashes + hangs;
    for (const auto count : counts) {
      total += count;
    }
    const auto share = [&](std::size_t count) {
      return total == 0 ? 0.0 : 100.0 * static_cast<double>(count) / static_cast<double>(total);
    };
    sheet::println("{:<14} {:>10} {:>8}", "stopped at", "inputs", "share");
    for (const auto stage : sheet::tools::all_stages) {
      const auto count = counts[static_cast<std::size_t>(stage)];
      sheet::println("{:<14} {:>10} {:>7.1f}%", to_string(stage), count, share(count));
    }
    sheet::println("{:<14} {:>10} {:>7.1f}%", "empty", empty, share(empty));
    sheet::println("{:<14} {:>10} {:>7.1f}%", "crash", crashes, share(crashes));
    sheet::println("{:<14} {:>10} {:>7.1f}%", "hang", hangs, share(hangs));
    sheet::println("{:<14} {:>10}", "total", total);
    if (crashes + hangs > 0) {
      std::fflush(stdout);
      sheet::println(stderr,
                     "{} inputs crashed or hung; list them with --csv and replay one with "
                     "sheet-repro to see its report",
                     crashes + hangs);
    }
  }
  return 0;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  try {
    return run(std::span{argv, static_cast<std::size_t>(argc)}.subspan(1));
  } catch (const std::exception& error) {
    std::fputs("sheet-stages: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
    return 2;
  }
}
