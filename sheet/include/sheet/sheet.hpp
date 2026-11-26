#pragma once

// Public API of the sheet library.
//
// The pipeline has three entry points that compose with std::expected:
//
//   auto result = sheet::parse(source)
//                     .and_then(sheet::check)
//                     .and_then(sheet::evaluate);
//
// Each call either succeeds or returns a Diagnostic naming the stage that
// rejected the input. Spreadsheet errors such as #DIV/0 are not rejections;
// they are ordinary values in the evaluated result.

#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sheet/ast.hpp"
#include "sheet/types.hpp"

namespace sheet {

// A sheet that passed name resolution, dependency analysis and type checking.
// It owns its Ast, so it is safe to keep after the parse result is gone.
class CheckedSheet {
public:
  CheckedSheet(Ast ast, std::vector<std::size_t> evaluation_order);

  [[nodiscard]] auto ast() const noexcept -> const Ast& { return ast_; }

  // Indices into ast().cells(), in an order that respects dependencies.
  [[nodiscard]] auto evaluation_order() const noexcept -> const std::vector<std::size_t>& {
    return evaluation_order_;
  }

  // The index into ast().cells() of the definition of `ref`, if any.
  [[nodiscard]] auto definition_of(CellRef ref) const -> std::optional<std::size_t>;

private:
  Ast ast_;
  std::vector<std::size_t> evaluation_order_;
  std::unordered_map<CellRef, std::size_t, CellRefHash> definitions_;
};

// The evaluated value of every defined cell, ordered by cell reference.
class Values {
public:
  explicit Values(std::map<CellRef, Value> values);

  [[nodiscard]] auto at(CellRef ref) const -> std::optional<Value>;
  [[nodiscard]] auto all() const noexcept -> const std::map<CellRef, Value>& { return values_; }

private:
  std::map<CellRef, Value> values_;
};

// Header, lexing and parsing.
[[nodiscard]] auto parse(std::string_view source) -> std::expected<Ast, Diagnostic>;

// Name resolution, dependency analysis and type checking. Takes ownership of
// the Ast so that `parse(source).and_then(check)` works directly.
[[nodiscard]] auto check(Ast ast) -> std::expected<CheckedSheet, Diagnostic>;

// Evaluation of every cell.
[[nodiscard]] auto evaluate(const CheckedSheet& sheet) -> std::expected<Values, Diagnostic>;

}  // namespace sheet
