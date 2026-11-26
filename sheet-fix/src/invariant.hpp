#pragma once

// Internal invariants: conditions that earlier stages guarantee. A violation
// is a bug in the library, never a property of the input, so it aborts in
// every build mode (unlike assert, which NDEBUG removes).

#include <optional>
#include <source_location>
#include <string_view>
#include <utility>

namespace sheet::detail {

[[noreturn]] auto invariant_failure(std::string_view message, std::source_location where) -> void;

inline auto invariant(bool condition, std::string_view message,
                      std::source_location where = std::source_location::current()) -> void {
  if (!condition) {
    invariant_failure(message, where);
  }
}

// Unwraps an optional that an earlier stage guarantees is engaged.
template <typename T>
[[nodiscard]] auto required(const std::optional<T>& value, std::string_view message,
                            std::source_location where = std::source_location::current())
    -> const T& {
  if (!value) {
    invariant_failure(message, where);
  }
  return *value;  // NOLINT(bugprone-unchecked-optional-access): checked above
}

template <typename T>
[[nodiscard]] auto required(std::optional<T>&& value, std::string_view message,
                            std::source_location where = std::source_location::current()) -> T {
  if (!value) {
    invariant_failure(message, where);
  }
  return *std::move(value);  // NOLINT(bugprone-unchecked-optional-access): checked above
}

}  // namespace sheet::detail
