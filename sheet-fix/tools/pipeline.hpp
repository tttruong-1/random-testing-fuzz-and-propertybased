#pragma once

// Helpers shared by the command-line tools. Not part of the library API:
// students write their own harness against sheet/sheet.hpp.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sheet/sheet.hpp"

namespace sheet::tools {

struct Outcome {
  Stage stage;                           // the rejecting stage, or Stage::Done
  std::optional<Diagnostic> diagnostic;  // set when rejected
  std::optional<Values> values;          // set when every stage passed
};

// A sheet with no cells passes every stage without exercising any of them,
// so the tools report it separately from real successes.
[[nodiscard]] inline auto is_empty_success(const Outcome& outcome) -> bool {
  return outcome.values && outcome.values->all().empty();
}

// "empty" for an accepted sheet with no cells, otherwise the stage name.
[[nodiscard]] inline auto label(const Outcome& outcome) -> std::string_view {
  return is_empty_success(outcome) ? "empty" : to_string(outcome.stage);
}

[[nodiscard]] inline auto run_pipeline(std::string_view source) -> Outcome {
  auto result = parse(source).and_then(check).and_then(evaluate);
  if (!result) {
    return Outcome{result.error().stage, std::move(result).error(), std::nullopt};
  }
  return Outcome{Stage::Done, std::nullopt, std::move(result).value()};
}

[[nodiscard]] inline auto read_file(const std::filesystem::path& path)
    -> std::optional<std::string> {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// Expands each argument: a directory yields its regular files (not
// recursive), a file yields itself. Each directory's files are sorted by name.
[[nodiscard]] inline auto collect_inputs(const std::vector<std::filesystem::path>& arguments)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> inputs;
  for (const auto& argument : arguments) {
    if (!std::filesystem::is_directory(argument)) {
      inputs.push_back(argument);
      continue;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{argument}) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path());
      }
    }
    std::ranges::sort(files);
    inputs.insert(inputs.end(), files.begin(), files.end());
  }
  return inputs;
}

inline constexpr auto all_stages = std::array{
    Stage::Header, Stage::Lex,   Stage::Parse, Stage::Resolve,
    Stage::Dependencies, Stage::Types, Stage::Eval, Stage::Done,
};

}  // namespace sheet::tools
