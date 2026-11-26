// sheet::evaluate: computes every cell of a checked sheet.
//
// Cells are taken from a worklist seeded with check()'s evaluation order. A
// cell whose inputs are not ready yet goes to the back of the list; with a
// correct order that never happens, but it keeps evaluation independent of
// how the order was produced.
//
// Spreadsheet errors (#DIV/0, #NUM, ...) are values: an operation on an error
// produces that error, so errors propagate to every dependent cell.

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "analysis.hpp"
#include "functions.hpp"
#include "invariant.hpp"
#include "sheet/sheet.hpp"

namespace sheet {
namespace {

using detail::Function;
using detail::invariant;

inline constexpr int max_round_digits = 15;

[[nodiscard]] auto error_of(const Value& value) -> std::optional<ErrorValue> {
  if (const auto* error = std::get_if<ErrorValue>(&value)) {
    return *error;
  }
  return std::nullopt;
}

[[nodiscard]] auto number_or_error(double number) -> Value {
  if (!std::isfinite(number)) {
    return ErrorValue{ErrorKind::Num};
  }
  return number;
}

// The text a value contributes to & and CONCAT. Not used for errors.
[[nodiscard]] auto text_of(const Value& value) -> std::string {
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  if (const auto* flag = std::get_if<bool>(&value)) {
    return *flag ? "TRUE" : "FALSE";
  }
  return std::format("{}", std::get<double>(value));
}

class Evaluator {
public:
  explicit Evaluator(const CheckedSheet& sheet)
      : sheet_{&sheet}, ast_{&sheet.ast()}, values_(sheet.ast().cells().size()) {}

  auto run() -> Values {
    const auto& order = sheet_->evaluation_order();
    std::deque<std::size_t> pending{order.begin(), order.end()};
    while (!pending.empty()) {
      const auto definition = pending.front();
      pending.pop_front();
      if (!ready(definition)) {
        pending.push_back(definition);
        continue;
      }
      values_[definition] = evaluate(ast_->cells()[definition].expression);
    }

    std::map<CellRef, Value> results;
    const auto cells = ast_->cells();
    for (std::size_t i = 0; i < cells.size(); ++i) {
      results.emplace(cells[i].target, detail::required(std::move(values_[i]), "cell left unevaluated"));
    }
    return Values{std::move(results)};
  }

private:
  // True when every cell that `definition` reads has a value.
  [[nodiscard]] auto ready(std::size_t definition) const -> bool {
    bool all_ready = true;
    auto on_cell = [&](CellRef ref) { all_ready = all_ready && has_value(ref); };
    auto on_range = [&](const detail::CellRange& range) {
      for (const auto covered : detail::covered_definitions(*ast_, range)) {
        all_ready = all_ready && values_[covered].has_value();
      }
    };
    detail::for_each_reference(*ast_, ast_->cells()[definition].expression, on_cell, on_range);
    return all_ready;
  }

  [[nodiscard]] auto has_value(CellRef ref) const -> bool {
    const auto definition = sheet_->definition_of(ref);
    return definition && values_[*definition].has_value();
  }

  [[nodiscard]] auto read(std::size_t definition) const -> const Value& {
    return detail::required(values_[definition], "cell read before it was evaluated");
  }

  [[nodiscard]] auto read(CellRef ref) const -> const Value& {
    return read(detail::required(sheet_->definition_of(ref),
                                 "reference to an undefined cell after resolution"));
  }

  [[nodiscard]] static auto number(const Value& value) -> double {
    const auto* result = std::get_if<double>(&value);
    invariant(result != nullptr, "type checker guaranteed a number");
    return *result;
  }

  [[nodiscard]] auto range_of(NodeId node) const -> const RangeReference* {
    return std::get_if<RangeReference>(&ast_->node(node));
  }

  [[nodiscard]] auto covered(const RangeReference& range) const -> std::vector<std::size_t> {
    return detail::covered_definitions(*ast_, detail::normalized(range.first, range.last));
  }

  auto evaluate(NodeId node) -> Value {
    const auto& current = ast_->node(node);
    if (const auto* literal = std::get_if<NumberLiteral>(&current)) {
      return literal->value;
    }
    if (const auto* literal = std::get_if<StringLiteral>(&current)) {
      return literal->value;
    }
    if (const auto* literal = std::get_if<BoolLiteral>(&current)) {
      return literal->value;
    }
    if (const auto* cell = std::get_if<CellReference>(&current)) {
      return read(cell->ref);
    }
    if (const auto* negate = std::get_if<Negate>(&current)) {
      const auto operand = evaluate(negate->operand);
      if (const auto error = error_of(operand)) {
        return *error;
      }
      return -number(operand);
    }
    if (const auto* plus = std::get_if<UnaryPlus>(&current)) {
      return evaluate(plus->operand);
    }
    if (const auto* binary = std::get_if<Binary>(&current)) {
      return evaluate_binary(*binary);
    }
    if (const auto* call = std::get_if<Call>(&current)) {
      return evaluate_call(*call);
    }
    invariant(false, "range outside a function argument");
    return ErrorValue{ErrorKind::Value};
  }

  auto evaluate_binary(const Binary& binary) -> Value {
    const auto lhs = evaluate(binary.lhs);
    if (const auto error = error_of(lhs)) {
      return *error;
    }
    const auto rhs = evaluate(binary.rhs);
    if (const auto error = error_of(rhs)) {
      return *error;
    }

    switch (binary.op) {
      case BinaryOp::Add: return number_or_error(number(lhs) + number(rhs));
      case BinaryOp::Sub: return number_or_error(number(lhs) - number(rhs));
      case BinaryOp::Mul: return number_or_error(number(lhs) * number(rhs));
      case BinaryOp::Div:
        if (number(rhs) == 0.0) {
          return ErrorValue{ErrorKind::DivZero};
        }
        return number_or_error(number(lhs) / number(rhs));
      case BinaryOp::Concat: return text_of(lhs) + text_of(rhs);
      case BinaryOp::Eq: return lhs == rhs;
      case BinaryOp::Ne: return lhs != rhs;
      case BinaryOp::Lt: return compare(lhs, rhs) < 0;
      case BinaryOp::Gt: return compare(lhs, rhs) > 0;
      case BinaryOp::Le: return compare(lhs, rhs) <= 0;
      case BinaryOp::Ge: return compare(lhs, rhs) >= 0;
    }
    invariant(false, "unhandled operator");
    return ErrorValue{ErrorKind::Value};
  }

  // The type checker guarantees two numbers or two strings.
  [[nodiscard]] static auto compare(const Value& lhs, const Value& rhs) -> std::partial_ordering {
    const auto* left_number = std::get_if<double>(&lhs);
    const auto* right_number = std::get_if<double>(&rhs);
    if (left_number != nullptr && right_number != nullptr) {
      return *left_number <=> *right_number;
    }
    const auto* left_text = std::get_if<std::string>(&lhs);
    const auto* right_text = std::get_if<std::string>(&rhs);
    invariant(left_text != nullptr && right_text != nullptr,
              "type checker guaranteed two numbers or two strings");
    return *left_text <=> *right_text;
  }

  // The numbers in the arguments of SUM, MIN, MAX and AVERAGE, or the first
  // error among them.
  auto collect_numbers(const Call& call) -> std::variant<std::vector<double>, ErrorValue> {
    std::vector<double> numbers;
    for (const auto argument : call.arguments) {
      if (const auto* range = range_of(argument)) {
        for (const auto definition : covered(*range)) {
          const auto& value = read(definition);
          if (const auto error = error_of(value)) {
            return *error;
          }
          numbers.push_back(number(value));
        }
        continue;
      }
      const auto value = evaluate(argument);
      if (const auto error = error_of(value)) {
        return *error;
      }
      numbers.push_back(number(value));
    }
    return numbers;
  }

  auto count(const Call& call) -> Value {
    double count = 0;
    const auto add = [&](const Value& value) {
      if (std::holds_alternative<double>(value)) {
        ++count;
      }
    };
    for (const auto argument : call.arguments) {
      if (const auto* range = range_of(argument)) {
        for (const auto definition : covered(*range)) {
          add(read(definition));
        }
      } else {
        add(evaluate(argument));
      }
    }
    return count;
  }

  // Text that is already stored (string literals in the Ast, string values
  // of cells) is viewed in place. Other text is converted into `owned`, a
  // deque, so earlier strings never move while pieces refer to them.
  auto concat(const Call& call) -> Value {
    std::vector<std::string_view> pieces;
    std::deque<std::string> owned;
    for (const auto argument : call.arguments) {
      if (const auto* literal = std::get_if<StringLiteral>(&ast_->node(argument))) {
        pieces.emplace_back(literal->value);
      } else if (const auto* range = range_of(argument)) {
        for (const auto definition : covered(*range)) {
          const auto& value = read(definition);
          if (const auto error = error_of(value)) {
            return *error;
          }
          if (const auto* text = std::get_if<std::string>(&value)) {
            pieces.emplace_back(*text);
          } else {
            pieces.emplace_back(text_of(value));
          }
        }
      } else {
        const auto value = evaluate(argument);
        if (const auto error = error_of(value)) {
          return *error;
        }
        pieces.emplace_back(owned.emplace_back(text_of(value)));
      }
    }

    std::size_t length = 0;
    for (const auto piece : pieces) {
      length += piece.size();
    }
    std::string result;
    result.reserve(length);
    for (const auto piece : pieces) {
      result.append(piece);
    }
    return result;
  }

  auto round(const Call& call) -> Value {
    const auto value = evaluate(call.arguments[0]);
    if (const auto error = error_of(value)) {
      return *error;
    }
    const auto digits_value = evaluate(call.arguments[1]);
    if (const auto error = error_of(digits_value)) {
      return *error;
    }
    const double digits = number(digits_value);
    if (digits != std::trunc(digits) || std::abs(digits) > max_round_digits) {
      return ErrorValue{ErrorKind::Num};
    }
    const double scale = std::pow(10.0, digits);
    return number_or_error(std::round(number(value) * scale) / scale);
  }

  auto evaluate_call(const Call& call) -> Value {
    const auto spec =
        detail::required(detail::find_function(call.name), "unknown function after resolution");

    switch (spec.id) {
      case Function::Sum:
      case Function::Min:
      case Function::Max:
      case Function::Average: {
        auto collected = collect_numbers(call);
        if (const auto* error = std::get_if<ErrorValue>(&collected)) {
          return *error;
        }
        const auto& numbers = std::get<std::vector<double>>(collected);
        if (spec.id == Function::Min || spec.id == Function::Max) {
          if (numbers.empty()) {
            return 0.0;  // as in common spreadsheet applications
          }
          return spec.id == Function::Min ? std::ranges::min(numbers) : std::ranges::max(numbers);
        }
        double sum = 0.0;
        for (const auto n : numbers) {
          sum += n;
        }
        if (spec.id == Function::Sum) {
          return number_or_error(sum);
        }
        if (numbers.empty()) {
          return ErrorValue{ErrorKind::DivZero};
        }
        return number_or_error(sum / static_cast<double>(numbers.size()));
      }
      case Function::Count: return count(call);
      case Function::If: {
        const auto condition = evaluate(call.arguments[0]);
        if (const auto error = error_of(condition)) {
          return *error;
        }
        const auto* flag = std::get_if<bool>(&condition);
        invariant(flag != nullptr, "type checker guaranteed a boolean condition");
        return evaluate(call.arguments[*flag ? 1 : 2]);
      }
      case Function::Concat: return concat(call);
      case Function::Len: {
        const auto text = evaluate(call.arguments[0]);
        if (const auto error = error_of(text)) {
          return *error;
        }
        const auto* string = std::get_if<std::string>(&text);
        invariant(string != nullptr, "type checker guaranteed a string");
        return static_cast<double>(string->size());
      }
      case Function::Round: return round(call);
    }
    invariant(false, "unhandled function");
    return ErrorValue{ErrorKind::Value};
  }

  const CheckedSheet* sheet_;
  const Ast* ast_;
  std::vector<std::optional<Value>> values_;  // by definition index
};

}  // namespace

Values::Values(std::map<CellRef, Value> values) : values_{std::move(values)} {}

auto Values::at(CellRef ref) const -> std::optional<Value> {
  if (const auto found = values_.find(ref); found != values_.end()) {
    return found->second;
  }
  return std::nullopt;
}

auto evaluate(const CheckedSheet& sheet) -> std::expected<Values, Diagnostic> {
  return Evaluator{sheet}.run();
}

}  // namespace sheet
