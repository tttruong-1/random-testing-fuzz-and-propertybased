// Stage Resolve: unique definitions, references to defined cells, and known
// functions called with a valid number of arguments.

#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analysis.hpp"
#include "functions.hpp"

namespace sheet::detail {
namespace {

class Resolver {
public:
  explicit Resolver(const Ast& ast)
      : ast_{&ast}, columns_in_use_(std::size_t{max_column_index} + 1, 0) {}

  auto run() -> std::expected<CellIndex, Diagnostic> {
    const auto cells = ast_->cells();
    index_.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
      const auto& definition = cells[i];
      if (!index_.emplace(definition.target, i).second) {
        return std::unexpected(error(
            std::format("cell {} is defined more than once", to_string(definition.target)),
            definition.location));
      }
      columns_in_use_[definition.target.column.index()] = 1;
    }
    for (const auto& definition : cells) {
      if (auto checked = check_node(definition.expression); !checked) {
        return std::unexpected(std::move(checked).error());
      }
    }
    return std::move(index_);
  }

private:
  [[nodiscard]] static auto error(std::string message, SourceLocation location) -> Diagnostic {
    return Diagnostic{Stage::Resolve, std::move(message), location};
  }

  // Most references in a typical sheet point at used columns; checking the
  // column first avoids a hash lookup for the rest.
  [[nodiscard]] auto is_defined(CellRef ref) const -> bool {
    return columns_in_use_[ref.column.index()] != 0 && index_.contains(ref);
  }

  auto check_node(NodeId id) -> std::expected<void, Diagnostic> {
    const auto& node = ast_->node(id);
    if (const auto* cell = std::get_if<CellReference>(&node)) {
      if (!is_defined(cell->ref)) {
        return std::unexpected(
            error(std::format("reference to undefined cell {}", to_string(cell->ref)),
                  ast_->location(id)));
      }
    } else if (const auto* negate = std::get_if<Negate>(&node)) {
      return check_node(negate->operand);
    } else if (const auto* plus = std::get_if<UnaryPlus>(&node)) {
      return check_node(plus->operand);
    } else if (const auto* binary = std::get_if<Binary>(&node)) {
      if (auto lhs = check_node(binary->lhs); !lhs) {
        return lhs;
      }
      return check_node(binary->rhs);
    } else if (const auto* call = std::get_if<Call>(&node)) {
      return check_call(*call, ast_->location(id));
    }
    // Literals need nothing; a range may cover undefined (empty) cells.
    return {};
  }

  auto check_call(const Call& call, SourceLocation location) -> std::expected<void, Diagnostic> {
    const auto spec = find_function(call.name);
    if (!spec) {
      return std::unexpected(error(std::format("unknown function {}", call.name), location));
    }
    const auto count = call.arguments.size();
    if (count < spec->min_arity || count > spec->max_arity) {
      const auto expected = spec->min_arity == spec->max_arity
                                ? std::format("{}", spec->min_arity)
                                : std::format("{} to {}", spec->min_arity, spec->max_arity);
      return std::unexpected(error(
          std::format("{} takes {} arguments, not {}", spec->name, expected, count), location));
    }
    for (const auto argument : call.arguments) {
      if (auto checked = check_node(argument); !checked) {
        return checked;
      }
    }
    return {};
  }

  const Ast* ast_;
  CellIndex index_;
  std::vector<std::uint8_t> columns_in_use_;
};

}  // namespace

auto resolve(const Ast& ast) -> std::expected<CellIndex, Diagnostic> {
  return Resolver{ast}.run();
}

}  // namespace sheet::detail
