#pragma once

// Core value types shared by every stage of the sheet pipeline.

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace sheet {

// The pipeline stages, in order. A Diagnostic names the stage that rejected
// an input; Stage::Done means the input passed every stage.
enum class Stage : std::uint8_t {
  Header,
  Lex,
  Parse,
  Resolve,
  Dependencies,
  Types,
  Eval,
  Done,
};

[[nodiscard]] constexpr auto to_string(Stage stage) noexcept -> std::string_view {
  switch (stage) {
    case Stage::Header: return "header";
    case Stage::Lex: return "lex";
    case Stage::Parse: return "parse";
    case Stage::Resolve: return "resolve";
    case Stage::Dependencies: return "dependencies";
    case Stage::Types: return "types";
    case Stage::Eval: return "eval";
    case Stage::Done: return "done";
  }
  return "unknown";
}

// A 1-based position in the source text.
struct SourceLocation {
  std::size_t line = 1;
  std::size_t column = 1;

  friend constexpr auto operator<=>(const SourceLocation&, const SourceLocation&) = default;
};

// Why an input was rejected. Rejections are not spreadsheet errors: a sheet
// that divides by zero is valid and evaluates to #DIV/0.
struct Diagnostic {
  Stage stage = Stage::Header;
  std::string message;
  SourceLocation location;
};

// Zero-based column index: A = 0, Z = 25, AA = 26.
class Column {
public:
  constexpr explicit Column(std::uint32_t index) noexcept : index_{index} {}

  [[nodiscard]] constexpr auto index() const noexcept -> std::uint32_t { return index_; }

  friend constexpr auto operator<=>(const Column&, const Column&) = default;

private:
  std::uint32_t index_;
};

// One-based row number, as written in the source.
class Row {
public:
  constexpr explicit Row(std::uint32_t number) noexcept : number_{number} {}

  [[nodiscard]] constexpr auto number() const noexcept -> std::uint32_t { return number_; }

  friend constexpr auto operator<=>(const Row&, const Row&) = default;

private:
  std::uint32_t number_;
};

// Limits matching common spreadsheet applications (column XFD, row 1048576).
inline constexpr std::uint32_t max_column_index = 16'383;
inline constexpr std::uint32_t max_row_number = 1'048'576;

struct CellRef {
  Column column;
  Row row;

  friend constexpr auto operator<=>(const CellRef&, const CellRef&) = default;
};

[[nodiscard]] auto to_string(CellRef ref) -> std::string;

struct CellRefHash {
  [[nodiscard]] auto operator()(const CellRef& ref) const noexcept -> std::size_t {
    const auto packed = (std::uint64_t{ref.column.index()} << 32U) | ref.row.number();
    return std::hash<std::uint64_t>{}(packed);
  }
};

// Spreadsheet error values. These propagate through dependent cells.
enum class ErrorKind : std::uint8_t {
  DivZero,  // #DIV/0
  Ref,      // #REF
  Value,    // #VALUE
  Num,      // #NUM
};

struct ErrorValue {
  ErrorKind kind;

  friend constexpr auto operator==(const ErrorValue&, const ErrorValue&) -> bool = default;
};

[[nodiscard]] auto to_string(ErrorValue error) -> std::string_view;

using Value = std::variant<double, std::string, bool, ErrorValue>;

[[nodiscard]] auto to_string(const Value& value) -> std::string;

}  // namespace sheet
