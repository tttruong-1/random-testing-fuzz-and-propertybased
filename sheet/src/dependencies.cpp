// Stage Dependencies: builds the dependency graph, rejects cycles and orders
// cells so that each comes after every cell it reads.
//
// Only cells that take part in a reference get a graph node; isolated cells
// (typically constants nobody reads) skip the graph and are ordered first.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "analysis.hpp"

namespace sheet::detail {
namespace {

// Typical sheets have fewer cells than this in their dependency graph.
inline constexpr std::size_t expected_graph_size = 64;

struct GraphNode {
  std::size_t definition;
  std::vector<std::size_t> dependents;  // node indices of cells that read this one
  std::size_t unresolved = 0;           // dependencies not yet ordered
};

class DependencyGraph {
public:
  DependencyGraph(const Ast& ast, const CellIndex& index)
      : ast_{&ast}, index_{&index}, node_of_(ast.cells().size()) {
    nodes_.reserve(expected_graph_size);
  }

  auto order() -> std::expected<std::vector<std::size_t>, Diagnostic> {
    build();

    std::vector<std::size_t> order;
    order.reserve(node_of_.size());
    for (std::size_t i = 0; i < node_of_.size(); ++i) {
      if (!node_of_[i]) {
        order.push_back(i);
      }
    }

    // Kahn's algorithm over the graph, in node creation order for determinism.
    std::deque<std::size_t> ready;
    for (std::size_t n = 0; n < nodes_.size(); ++n) {
      if (nodes_[n].unresolved == 0) {
        ready.push_back(n);
      }
    }
    while (!ready.empty()) {
      const auto n = ready.front();
      ready.pop_front();
      order.push_back(nodes_[n].definition);
      for (const auto dependent : nodes_[n].dependents) {
        if (--nodes_[dependent].unresolved == 0) {
          ready.push_back(dependent);
        }
      }
    }

    if (order.size() != node_of_.size()) {
      return std::unexpected(cycle_error());
    }
    return order;
  }

private:
  // The definitions that cell `definition` reads, without duplicates.
  [[nodiscard]] auto reads(std::size_t definition) const -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    auto on_cell = [&](CellRef ref) { result.push_back(index_->at(ref)); };
    auto on_range = [&](const CellRange& range) {
      for (const auto covered : covered_definitions(*ast_, range)) {
        // A total such as A10 = SUM(A1:A10) does not depend on itself.
        if (covered == definition) {
          continue;
        }
        result.push_back(covered);
      }
    };
    for_each_reference(*ast_, ast_->cells()[definition].expression, on_cell, on_range);
    std::ranges::sort(result);
    const auto duplicates = std::ranges::unique(result);
    result.erase(duplicates.begin(), duplicates.end());
    return result;
  }

  auto node_for(std::size_t definition) -> std::size_t {
    auto& slot = node_of_[definition];
    if (!slot) {
      slot = nodes_.size();
      nodes_.push_back(GraphNode{.definition = definition, .dependents = {}});
    }
    return *slot;
  }

  auto build() -> void {
    for (std::size_t i = 0; i < node_of_.size(); ++i) {
      for (const auto read : reads(i)) {
        nodes_[node_for(read)].dependents.push_back(node_for(i));
        ++nodes_[node_for(i)].unresolved;
      }
    }
  }

  [[nodiscard]] auto cycle_error() const -> Diagnostic {
    // Report the earliest-defined cell still waiting on a dependency.
    auto earliest = std::numeric_limits<std::size_t>::max();
    for (const auto& node : nodes_) {
      if (node.unresolved > 0) {
        earliest = std::min(earliest, node.definition);
      }
    }
    const auto& definition = ast_->cells()[earliest];
    return Diagnostic{
        Stage::Dependencies,
        std::format("circular reference involving cell {}", to_string(definition.target)),
        definition.location};
  }

  const Ast* ast_;
  const CellIndex* index_;
  std::vector<std::optional<std::size_t>> node_of_;  // by definition index
  std::vector<GraphNode> nodes_;
};

}  // namespace

auto order_cells(const Ast& ast, const CellIndex& index)
    -> std::expected<std::vector<std::size_t>, Diagnostic> {
  return DependencyGraph{ast, index}.order();
}

}  // namespace sheet::detail
