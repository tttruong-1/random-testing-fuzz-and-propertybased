// covtrace: coverage over time for a corpus, measured identically for every
// fuzzing configuration.
//
//   covtrace [--start EPOCH_SECONDS] [--timeout SECONDS] [--verbose] DIR_OR_FILE...
//
// Inputs are replayed in modification-time order (then by name). Each input
// runs in a forked child, so crashes and hangs are recorded rather than
// ending the replay. Output is CSV on stdout, one row per input:
//
//   time_s,inputs,edges,stage,status,file
//
//   time_s  input mtime minus the start time (default: the earliest mtime)
//   inputs  number of inputs replayed so far, including this one
//   edges   cumulative distinct edges covered in the sheet library
//   stage   stage that rejected this input, "done", "empty" (accepted, no
//           cells) or "-" (crashed or timed out before finishing)
//   status  ok | timeout | signal:<number> | exit:<code>
//
// libFuzzer corpora keep only inputs that added coverage, so replaying them
// in mtime order reconstructs the coverage curve of the fuzzing run. Run
// libFuzzer with -reduce_inputs=0 so that earlier inputs are not deleted.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
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
#include "sheet/print.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "covtrace_runtime.hpp"
#include "pipeline.hpp"

// Hardening violations trap with SIGILL; ask ASan to report them with a
// stack trace, as libFuzzer does. Users can still override ASAN_OPTIONS.
extern "C" auto __asan_default_options() -> const char* {  // NOLINT(bugprone-reserved-identifier,readability-identifier-naming)
  return "handle_sigill=1:handle_abort=1";
}

namespace {

struct Options {
  std::optional<double> start;
  unsigned timeout_seconds = 5;
  bool verbose = false;
  std::vector<std::filesystem::path> paths;
};

struct Input {
  std::filesystem::path path;
  double mtime;
};

// Written by the child, read by the parent.
struct SharedResult {
  static constexpr std::uint8_t unknown = 0xFF;  // the child died before finishing
  static constexpr std::uint8_t empty = 0xFE;    // accepted, but defines no cells
  std::uint8_t stage = unknown;
};

auto print_usage() -> void {
  sheet::println(stderr,
               "usage: covtrace [--start EPOCH_SECONDS] [--timeout SECONDS] [--verbose] "
               "DIR_OR_FILE...");
}

template <typename T>
auto parse_value(std::string_view text) -> std::optional<T> {
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

auto parse_options(std::span<char*> args) -> std::optional<Options> {
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    const auto next = [&]() -> std::optional<std::string_view> {
      if (i + 1 >= args.size()) {
        return std::nullopt;
      }
      return std::string_view{args[++i]};
    };
    if (arg == "--start") {
      const auto text = next();
      // Seconds since the epoch; integral is enough for 30-minute trials.
      const auto value = text ? parse_value<long long>(*text) : std::nullopt;
      if (!value) {
        return std::nullopt;
      }
      options.start = static_cast<double>(*value);
    } else if (arg == "--timeout") {
      const auto text = next();
      const auto value = text ? parse_value<unsigned>(*text) : std::nullopt;
      if (!value || *value == 0) {
        return std::nullopt;
      }
      options.timeout_seconds = *value;
    } else if (arg == "--verbose") {
      options.verbose = true;
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

auto modification_time(const std::filesystem::path& path) -> std::optional<double> {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) {
    return std::nullopt;
  }
  return static_cast<double>(info.st_mtim.tv_sec) + static_cast<double>(info.st_mtim.tv_nsec) * 1e-9;
}

auto ordered_inputs(const std::vector<std::filesystem::path>& paths) -> std::vector<Input> {
  std::vector<Input> inputs;
  for (const auto& path : sheet::tools::collect_inputs(paths)) {
    if (const auto mtime = modification_time(path)) {
      inputs.push_back(Input{path, *mtime});
    }
  }
  std::ranges::stable_sort(inputs, {}, &Input::mtime);
  return inputs;
}

auto silence_stderr() -> void {
  const int null = open("/dev/null", O_WRONLY);
  if (null >= 0) {
    dup2(null, STDERR_FILENO);
    close(null);
  }
}

// Runs one input in a child process; returns the status column.
auto run_child(const Input& input, SharedResult& result, const Options& options) -> std::string {
  result.stage = SharedResult::unknown;
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("covtrace: fork");
    std::exit(1);
  }
  if (pid == 0) {
    if (!options.verbose) {
      silence_stderr();
    }
    alarm(options.timeout_seconds);
    const auto source = sheet::tools::read_file(input.path);
    if (source) {
      const auto outcome = sheet::tools::run_pipeline(*source);
      result.stage = sheet::tools::is_empty_success(outcome)
                         ? SharedResult::empty
                         : static_cast<std::uint8_t>(outcome.stage);
    }
    _exit(0);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
  }
  if (WIFSIGNALED(status)) {
    return WTERMSIG(status) == SIGALRM ? "timeout" : std::format("signal:{}", WTERMSIG(status));
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    return std::format("exit:{}", WEXITSTATUS(status));
  }
  return "ok";
}

auto count_edges(std::span<const std::uint8_t> map) -> std::size_t {
  return static_cast<std::size_t>(std::ranges::count_if(map, [](std::uint8_t hit) { return hit != 0; }));
}

auto run(std::span<char*> args) -> int {
  const auto options = parse_options(args);
  if (!options) {
    print_usage();
    return 2;
  }
  const auto map = covtrace::coverage_map();
  if (map.empty()) {
    sheet::println(stderr, "covtrace: no instrumented code; build with the covtrace preset");
    return 1;
  }

  void* shared = mmap(nullptr, sizeof(SharedResult), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shared == MAP_FAILED) {
    std::perror("covtrace: mmap");
    return 1;
  }
  auto& result = *static_cast<SharedResult*>(shared);

  const auto inputs = ordered_inputs(options->paths);
  const double start = options->start.value_or(inputs.empty() ? 0.0 : inputs.front().mtime);

  sheet::println("time_s,inputs,edges,stage,status,file");
  std::size_t replayed = 0;
  for (const auto& input : inputs) {
    const auto status = run_child(input, result, *options);
    ++replayed;
    const auto stage = result.stage == SharedResult::unknown ? std::string_view{"-"}
                       : result.stage == SharedResult::empty ? std::string_view{"empty"}
                                                              : to_string(static_cast<sheet::Stage>(result.stage));
    sheet::println("{:.3f},{},{},{},{},{}", input.mtime - start, replayed, count_edges(map), stage,
                 status, input.path.filename().string());
  }
  return 0;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  try {
    return run(std::span{argv, static_cast<std::size_t>(argc)}.subspan(1));
  } catch (const std::exception& error) {
    std::fputs("covtrace: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
    return 2;
  }
}
