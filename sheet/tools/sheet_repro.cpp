// sheet-repro: runs one input through the pipeline and reports the result.
//
//   sheet-repro FILE
//
// Build it with the repro preset (sanitizers and hardening on) so that a
// crashing input produces the same report as under the fuzzer.

#include <cstddef>
#include <cstdio>
#include <exception>
#include "sheet/print.hpp"
#include <span>
#include <string_view>

#include "pipeline.hpp"

// Hardening violations trap with SIGILL; ask ASan to report them with a
// stack trace, as libFuzzer does. Users can still override ASAN_OPTIONS.
extern "C" auto __asan_default_options() -> const char* {  // NOLINT(bugprone-reserved-identifier,readability-identifier-naming)
  return "handle_sigill=1:handle_abort=1";
}

namespace {

auto run(std::span<char*> args) -> int {
  if (args.size() != 1) {
    sheet::println(stderr, "usage: sheet-repro FILE");
    return 2;
  }
  const auto source = sheet::tools::read_file(args[0]);
  if (!source) {
    sheet::println(stderr, "error: cannot read {}", args[0]);
    return 2;
  }

  const auto outcome = sheet::tools::run_pipeline(*source);
  if (outcome.diagnostic) {
    const auto& d = *outcome.diagnostic;
    sheet::println("rejected at {} ({}:{}): {}", to_string(d.stage), d.location.line,
                 d.location.column, d.message);
    return 1;
  }
  sheet::println("accepted");
  if (outcome.values) {
    for (const auto& [ref, value] : outcome.values->all()) {
      sheet::println("  {} = {}", to_string(ref), to_string(value));
    }
  }
  return 0;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  try {
    return run(std::span{argv, static_cast<std::size_t>(argc)}.subspan(1));
  } catch (const std::exception& error) {
    std::fputs("sheet-repro: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
    return 2;
  }
}
