// sheet::check: name resolution, dependency analysis and type checking.

#include <utility>

#include "analysis.hpp"
#include "sheet/sheet.hpp"

namespace sheet {

CheckedSheet::CheckedSheet(Ast ast, std::vector<std::size_t> evaluation_order)
    : ast_{std::move(ast)}, evaluation_order_{std::move(evaluation_order)} {
  const auto cells = ast_.cells();
  definitions_.reserve(cells.size());
  for (std::size_t i = 0; i < cells.size(); ++i) {
    definitions_.emplace(cells[i].target, i);
  }
}

auto CheckedSheet::definition_of(CellRef ref) const -> std::optional<std::size_t> {
  if (const auto found = definitions_.find(ref); found != definitions_.end()) {
    return found->second;
  }
  return std::nullopt;
}

auto check(Ast ast) -> std::expected<CheckedSheet, Diagnostic> {
  auto index = detail::resolve(ast);
  if (!index) {
    return std::unexpected(std::move(index).error());
  }
  auto order = detail::order_cells(ast, *index);
  if (!order) {
    return std::unexpected(std::move(order).error());
  }
  if (auto typed = detail::check_types(ast, *index, *order); !typed) {
    return std::unexpected(std::move(typed).error());
  }
  return CheckedSheet{std::move(ast), std::move(*order)};
}

}  // namespace sheet
