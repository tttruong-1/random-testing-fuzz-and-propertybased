#include <algorithm>
#include <utility>

#include "analysis.hpp"

namespace sheet::detail {

auto normalized(CellRef a, CellRef b) noexcept -> CellRange {
  return CellRange{
      CellRef{std::min(a.column, b.column), std::min(a.row, b.row)},
      CellRef{std::max(a.column, b.column), std::max(a.row, b.row)},
  };
}

auto contains(const CellRange& range, CellRef ref) noexcept -> bool {
  return range.first.column <= ref.column && ref.column <= range.last.column &&
         range.first.row <= ref.row && ref.row <= range.last.row;
}

auto covered_definitions(const Ast& ast, const CellRange& range) -> std::vector<std::size_t> {
  const auto cells = ast.cells();
  std::vector<std::size_t> covered;
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (contains(range, cells[i].target)) {
      covered.push_back(i);
    }
  }
  std::ranges::sort(covered, [&](std::size_t a, std::size_t b) {
    const auto& lhs = cells[a].target;
    const auto& rhs = cells[b].target;
    return std::pair{lhs.row, lhs.column} < std::pair{rhs.row, rhs.column};
  });
  return covered;
}

}  // namespace sheet::detail
