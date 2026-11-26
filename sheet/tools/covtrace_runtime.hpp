#pragma once

// A minimal -fsanitize-coverage=trace-pc-guard runtime. Every instrumented
// edge in the sheet library gets an id; the first time an edge runs, its byte
// in a shared coverage map is set. The map lives in MAP_SHARED memory, so a
// forked child's coverage is visible to the parent.

#include <cstdint>
#include <span>

namespace covtrace {

// Bytes indexed by edge id; index 0 is unused. Empty if nothing is instrumented.
[[nodiscard]] auto coverage_map() noexcept -> std::span<std::uint8_t>;

}  // namespace covtrace
