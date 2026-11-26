#pragma once

// The built-in functions. Resolution checks names and arity against this
// table; the type checker and evaluator dispatch on Function.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sheet::detail {

enum class Function : std::uint8_t { Sum, Min, Max, Average, Count, If, Concat, Len, Round };

struct FunctionSpec {
  Function id;
  std::string_view name;
  std::size_t min_arity;
  std::size_t max_arity;
  bool accepts_ranges;
};

inline constexpr std::size_t max_arguments = 255;

inline constexpr auto functions = std::array{
    FunctionSpec{Function::Sum, "SUM", 1, max_arguments, true},
    FunctionSpec{Function::Min, "MIN", 1, max_arguments, true},
    FunctionSpec{Function::Max, "MAX", 1, max_arguments, true},
    FunctionSpec{Function::Average, "AVERAGE", 1, max_arguments, true},
    FunctionSpec{Function::Count, "COUNT", 1, max_arguments, true},
    FunctionSpec{Function::If, "IF", 3, 3, false},
    FunctionSpec{Function::Concat, "CONCAT", 1, max_arguments, true},
    FunctionSpec{Function::Len, "LEN", 1, 1, false},
    FunctionSpec{Function::Round, "ROUND", 2, 2, false},
};

[[nodiscard]] constexpr auto find_function(std::string_view name) -> std::optional<FunctionSpec> {
  const auto* found = std::ranges::find(functions, name, &FunctionSpec::name);
  if (found == functions.end()) {
    return std::nullopt;
  }
  return *found;
}

}  // namespace sheet::detail
