#pragma once

// The syntax tree produced by sheet::parse. Nodes live in an arena owned by
// the Ast and refer to each other by NodeId, so the tree needs no individual
// allocations and is freed in one step.

#include <compare>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "sheet/types.hpp"

namespace sheet {

struct NodeId {
  std::uint32_t value = 0;

  friend constexpr auto operator<=>(const NodeId&, const NodeId&) = default;
};

enum class BinaryOp : std::uint8_t {
  Add, Sub, Mul, Div, Concat,
  Eq, Ne, Lt, Gt, Le, Ge,
};

[[nodiscard]] auto to_string(BinaryOp op) -> std::string_view;

struct NumberLiteral {
  double value;
};

struct StringLiteral {
  std::string value;  // escapes already decoded
};

struct BoolLiteral {
  bool value;
};

struct CellReference {
  CellRef ref;
};

// Only valid as a function argument.
struct RangeReference {
  CellRef first;
  CellRef last;
};

struct Negate {
  NodeId operand;
};

// +x: the value of x, which must be a number.
struct UnaryPlus {
  NodeId operand;
};

struct Binary {
  BinaryOp op;
  NodeId lhs;
  NodeId rhs;
};

struct Call {
  std::string_view name;  // a view into the Ast's source; see Ast
  std::vector<NodeId> arguments;
};

using Node = std::variant<NumberLiteral, StringLiteral, BoolLiteral, CellReference,
                          RangeReference, Negate, UnaryPlus, Binary, Call>;

struct CellDefinition {
  CellRef target{Column{0}, Row{1}};
  NodeId expression;
  SourceLocation location;
};

// Owns the source text and every node. The source is held on the heap so that
// string_views into it (such as Call::name) stay valid when the Ast is moved.
class Ast {
public:
  explicit Ast(std::string source);

  [[nodiscard]] auto source() const noexcept -> std::string_view { return *source_; }

  auto add(Node node, SourceLocation location) -> NodeId;
  auto add_cell(CellDefinition definition) -> void;

  [[nodiscard]] auto node(NodeId id) const -> const Node&;
  [[nodiscard]] auto location(NodeId id) const -> SourceLocation;
  [[nodiscard]] auto cells() const noexcept -> std::span<const CellDefinition> { return cells_; }
  [[nodiscard]] auto node_count() const noexcept -> std::size_t { return nodes_.size(); }

private:
  std::unique_ptr<const std::string> source_;
  std::vector<Node> nodes_;
  std::vector<SourceLocation> locations_;
  std::vector<CellDefinition> cells_;
};

}  // namespace sheet
