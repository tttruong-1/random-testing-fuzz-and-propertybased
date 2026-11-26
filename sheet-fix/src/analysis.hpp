#pragma once

// Internal: the stages behind sheet::check, and helpers shared with the
// evaluator.

#include <cstddef>
#include <expected>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

#include "sheet/ast.hpp"
#include "sheet/types.hpp"

namespace sheet::detail {

using CellIndex = std::unordered_map<CellRef, std::size_t, CellRefHash>;

// A rectangle of cells with first at the top left and last at the bottom
// right. Build one with normalized(); a range may be written either way
// round (B3:B1 means B1:B3).
struct CellRange {
  CellRef first;
  CellRef last;
};

[[nodiscard]] auto normalized(CellRef a, CellRef b) noexcept -> CellRange;
[[nodiscard]] auto contains(const CellRange& range, CellRef ref) noexcept -> bool;

// Indices of the defined cells inside `range`, in row-major order (row by
// row, left to right). Undefined cells in a range are simply empty.
[[nodiscard]] auto covered_definitions(const Ast& ast, const CellRange& range)
    -> std::vector<std::size_t>;

// Calls on_cell(CellRef) for every direct reference and on_range(CellRange)
// for every range in the expression rooted at `node`.
template <typename OnCell, typename OnRange>
auto for_each_reference(const Ast& ast, NodeId node, OnCell& on_cell, OnRange& on_range) -> void {
  const auto& current = ast.node(node);
  if (const auto* cell = std::get_if<CellReference>(&current)) {
    on_cell(cell->ref);
  } else if (const auto* range = std::get_if<RangeReference>(&current)) {
    on_range(normalized(range->first, range->last));
  } else if (const auto* negate = std::get_if<Negate>(&current)) {
    for_each_reference(ast, negate->operand, on_cell, on_range);
  } else if (const auto* plus = std::get_if<UnaryPlus>(&current)) {
    for_each_reference(ast, plus->operand, on_cell, on_range);
  } else if (const auto* binary = std::get_if<Binary>(&current)) {
    for_each_reference(ast, binary->lhs, on_cell, on_range);
    for_each_reference(ast, binary->rhs, on_cell, on_range);
  } else if (const auto* call = std::get_if<Call>(&current)) {
    for (const auto argument : call->arguments) {
      for_each_reference(ast, argument, on_cell, on_range);
    }
  }
}

// Stage Resolve: unique definitions, defined references, known functions.
[[nodiscard]] auto resolve(const Ast& ast) -> std::expected<CellIndex, Diagnostic>;

// Stage Dependencies: rejects cycles; returns definition indices in an order
// where every cell comes after the cells it reads.
[[nodiscard]] auto order_cells(const Ast& ast, const CellIndex& index)
    -> std::expected<std::vector<std::size_t>, Diagnostic>;

// Stage Types: static types of every expression.
[[nodiscard]] auto check_types(const Ast& ast, const CellIndex& index,
                               std::span<const std::size_t> order)
    -> std::expected<void, Diagnostic>;

}  // namespace sheet::detail
