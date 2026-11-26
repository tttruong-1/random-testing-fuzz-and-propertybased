#include "sheet/ast.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace sheet {

auto to_string(BinaryOp op) -> std::string_view {
  switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Concat: return "&";
    case BinaryOp::Eq: return "==";
    case BinaryOp::Ne: return "<>";
    case BinaryOp::Lt: return "<";
    case BinaryOp::Gt: return ">";
    case BinaryOp::Le: return "<=";
    case BinaryOp::Ge: return ">=";
  }
  return "?";
}

Ast::Ast(std::string source) : source_{std::make_unique<const std::string>(std::move(source))} {}

auto Ast::add(Node node, SourceLocation location) -> NodeId {
  if (nodes_.size() >= std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error{"too many AST nodes"};
  }
  const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
  nodes_.push_back(std::move(node));
  locations_.push_back(location);
  return id;
}

auto Ast::add_cell(CellDefinition definition) -> void { cells_.push_back(definition); }

auto Ast::node(NodeId id) const -> const Node& { return nodes_[id.value]; }

auto Ast::location(NodeId id) const -> SourceLocation { return locations_[id.value]; }

}  // namespace sheet
