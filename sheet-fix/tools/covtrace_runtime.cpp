// This file must NOT be compiled with -fsanitize-coverage: it implements the
// callbacks that the instrumented library calls.

#include "covtrace_runtime.hpp"

#include <sys/mman.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr std::size_t map_capacity = std::size_t{1} << 22;

std::uint8_t* coverage = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::uint32_t edge_count = 0;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto ensure_map() -> void {
  if (coverage != nullptr) {
    return;
  }
  void* memory = mmap(nullptr, map_capacity, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                      -1, 0);
  if (memory == MAP_FAILED) {
    std::perror("covtrace: mmap");
    std::abort();
  }
  coverage = static_cast<std::uint8_t*>(memory);
}

}  // namespace

// Callback names and signatures are fixed by -fsanitize-coverage.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming,readability-non-const-parameter)
extern "C" void __sanitizer_cov_trace_pc_guard_init(std::uint32_t* start, std::uint32_t* stop) {
  if (start == stop || *start != 0) {
    return;  // already initialized for this module
  }
  ensure_map();
  for (auto* guard = start; guard < stop; ++guard) {
    if (edge_count + 1 >= map_capacity) {
      *guard = 0;  // beyond capacity: leave this edge untracked
      continue;
    }
    *guard = ++edge_count;
  }
}

// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
extern "C" void __sanitizer_cov_trace_pc_guard(std::uint32_t* guard) {
  const auto id = *guard;
  if (id == 0) {
    return;
  }
  coverage[id] = 1;
  *guard = 0;  // each edge only needs recording once per process
}

namespace covtrace {

auto coverage_map() noexcept -> std::span<std::uint8_t> {
  if (coverage == nullptr) {
    return {};
  }
  return {coverage, std::size_t{edge_count} + 1};
}

}  // namespace covtrace
