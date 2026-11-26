// Stage Types: every expression has a static type (number, string or bool),
// checked cell by cell in evaluation order. The evaluator relies on these
// guarantees and treats a violation as a bug in the library.

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "analysis.hpp"
#include "functions.hpp"
#include "invariant.hpp"

namespace sheet::detail {
namespace {

enum class Type : std::uint8_t { Number, String, Bool };

[[nodiscard]] constexpr auto describe(Type type) noexcept -> std::string_view {
  switch (type) {
    case Type::Number: return "a number";
    case Type::String: return "a string";
    case Type::Bool: return "a boolean";
  }
  return "a value";
}

[[nodiscard]] constexpr auto is_arithmetic(BinaryOp op) noexcept -> bool {
  return op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::Div;
}

[[nodiscard]] constexpr auto is_ordering(BinaryOp op) noexcept -> bool {
  return op == BinaryOp::Lt || op == BinaryOp::Gt || op == BinaryOp::Le || op == BinaryOp::Ge;
}

class TypeChecker {
public:
  TypeChecker(const Ast& ast, const CellIndex& index)
      : ast_{&ast}, index_{&index}, cell_types_(ast.cells().size()) {}

  auto run(std::span<const std::size_t> order) -> std::expected<void, Diagnostic> {
    for (const auto definition : order) {
      auto type = infer(ast_->cells()[definition].expression);
      if (!type) {
        return std::unexpected(std::move(type).error());
      }
      cell_types_[definition] = *type;
    }
    return {};
  }

private:
  using TypeResult = std::expected<Type, Diagnostic>;

  [[nodiscard]] auto error(NodeId node, std::string message) const -> Diagnostic {
    return Diagnostic{Stage::Types, std::move(message), ast_->location(node)};
  }

  [[nodiscard]] auto type_of_cell(std::size_t definition) const -> Type {
    return required(cell_types_[definition], "cell read before its type was inferred");
  }

  // Requires `node` to have type `wanted`.
  auto expect(NodeId node, Type wanted, std::string_view context) -> std::expected<void, Diagnostic> {
    auto type = infer(node);
    if (!type) {
      return std::unexpected(std::move(type).error());
    }
    if (*type != wanted) {
      return std::unexpected(
          error(node, std::format("{} needs {}, not {}", context, describe(wanted), describe(*type))));
    }
    return {};
  }

  auto infer(NodeId node) -> TypeResult {
    const auto& current = ast_->node(node);
    if (std::holds_alternative<NumberLiteral>(current)) {
      return Type::Number;
    }
    if (std::holds_alternative<StringLiteral>(current)) {
      return Type::String;
    }
    if (std::holds_alternative<BoolLiteral>(current)) {
      return Type::Bool;
    }
    if (const auto* cell = std::get_if<CellReference>(&current)) {
      return type_of_cell(index_->at(cell->ref));
    }
    if (const auto* negate = std::get_if<Negate>(&current)) {
      if (auto checked = expect(negate->operand, Type::Number, "negation"); !checked) {
        return std::unexpected(std::move(checked).error());
      }
      return Type::Number;
    }
    if (const auto* plus = std::get_if<UnaryPlus>(&current)) {
      if (auto checked = expect(plus->operand, Type::Number, "unary plus"); !checked) {
        return std::unexpected(std::move(checked).error());
      }
      return Type::Number;
    }
    if (const auto* binary = std::get_if<Binary>(&current)) {
      return infer_binary(node, *binary);
    }
    if (const auto* call = std::get_if<Call>(&current)) {
      return infer_call(node, *call);
    }
    invariant(false, "range outside a function argument");
    return Type::Number;
  }

  auto infer_binary(NodeId node, const Binary& binary) -> TypeResult {
    const auto context = std::format("operator {}", to_string(binary.op));
    if (is_arithmetic(binary.op)) {
      if (auto lhs = expect(binary.lhs, Type::Number, context); !lhs) {
        return std::unexpected(std::move(lhs).error());
      }
      if (auto rhs = expect(binary.rhs, Type::Number, context); !rhs) {
        return std::unexpected(std::move(rhs).error());
      }
      return Type::Number;
    }

    auto lhs = infer(binary.lhs);
    if (!lhs) {
      return lhs;
    }
    auto rhs = infer(binary.rhs);
    if (!rhs) {
      return rhs;
    }
    if (binary.op == BinaryOp::Concat) {
      return Type::String;
    }
    if (*lhs != *rhs) {
      return std::unexpected(error(node, std::format("{} cannot compare {} with {}", context,
                                                     describe(*lhs), describe(*rhs))));
    }
    if (is_ordering(binary.op) && *lhs == Type::Bool) {
      return std::unexpected(error(node, std::format("{} cannot order booleans", context)));
    }
    return Type::Bool;
  }

  // Every defined cell in the range must hold a number.
  auto expect_numeric_range(NodeId node, const RangeReference& range, std::string_view context)
      -> std::expected<void, Diagnostic> {
    const CellRange cells{range.first, range.last};
    for (const auto covered : covered_definitions(*ast_, cells)) {
      if (const auto type = type_of_cell(covered); type != Type::Number) {
        return std::unexpected(error(
            node, std::format("{} needs numbers, but cell {} holds {}", context,
                              to_string(ast_->cells()[covered].target), describe(type))));
      }
    }
    return {};
  }

  auto infer_call(NodeId node, const Call& call) -> TypeResult {
    const auto spec = required(find_function(call.name), "unknown function after resolution");
    const auto& arguments = call.arguments;

    if (!spec.accepts_ranges) {
      for (const auto argument : arguments) {
        if (std::holds_alternative<RangeReference>(ast_->node(argument))) {
          return std::unexpected(
              error(argument, std::format("{} does not accept a range", spec.name)));
        }
      }
    }

    switch (spec.id) {
      case Function::Sum:
      case Function::Min:
      case Function::Max:
      case Function::Average:
        for (const auto argument : arguments) {
          const auto* range = std::get_if<RangeReference>(&ast_->node(argument));
          auto checked = range != nullptr ? expect_numeric_range(argument, *range, spec.name)
                                          : expect(argument, Type::Number, spec.name);
          if (!checked) {
            return std::unexpected(std::move(checked).error());
          }
        }
        return Type::Number;

      case Function::Count:
      case Function::Concat:
        // Any values; ranges may hold anything.
        for (const auto argument : arguments) {
          if (std::holds_alternative<RangeReference>(ast_->node(argument))) {
            continue;
          }
          if (auto type = infer(argument); !type) {
            return type;
          }
        }
        return spec.id == Function::Count ? Type::Number : Type::String;

      case Function::If: {
        if (auto condition = expect(arguments[0], Type::Bool, "IF condition"); !condition) {
          return std::unexpected(std::move(condition).error());
        }
        auto then_type = infer(arguments[1]);
        if (!then_type) {
          return then_type;
        }
        auto else_type = infer(arguments[2]);
        if (!else_type) {
          return else_type;
        }
        if (*then_type != *else_type) {
          return std::unexpected(error(node, std::format("IF branches differ: {} and {}",
                                                         describe(*then_type),
                                                         describe(*else_type))));
        }
        return *then_type;
      }

      case Function::Len:
        if (auto text = expect(arguments[0], Type::String, "LEN"); !text) {
          return std::unexpected(std::move(text).error());
        }
        return Type::Number;

      case Function::Round:
        for (const auto argument : arguments) {
          if (auto number = expect(argument, Type::Number, "ROUND"); !number) {
            return std::unexpected(std::move(number).error());
          }
        }
        return Type::Number;
    }
    invariant(false, "unhandled function");
    return Type::Number;
  }

  const Ast* ast_;
  const CellIndex* index_;
  std::vector<std::optional<Type>> cell_types_;  // by definition index
};

}  // namespace

auto check_types(const Ast& ast, const CellIndex& index, std::span<const std::size_t> order)
    -> std::expected<void, Diagnostic> {
  return TypeChecker{ast, index}.run(order);
}

}  // namespace sheet::detail
