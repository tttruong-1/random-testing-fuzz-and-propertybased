// Header check, lexing and recursive-descent parsing: sheet::parse.

#include <algorithm>
#include <cerrno>
#include <charconv>  // defines __cpp_lib_to_chars when floating-point from_chars exists
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "lexer.hpp"
#include "sheet/sheet.hpp"

namespace sheet {
namespace {

using detail::Token;
using detail::TokenKind;

// Deeper input is rejected rather than risking stack exhaustion, both while
// parsing (recursion depth) and in later stages, which walk the tree
// recursively (tree height; long chains such as 1+1+...+1 are deep trees
// even though the parser builds them with a loop).
inline constexpr std::size_t max_nesting_depth = 256;
inline constexpr std::uint32_t max_tree_height = 256;

[[nodiscard]] auto parse_number(std::string_view text) -> std::optional<double> {
#if defined(__cpp_lib_to_chars)
  double value = 0.0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
#else
  // Fallback for standard libraries without floating-point from_chars.
  const std::string copy{text};
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(copy.c_str(), &end);
  if (errno == ERANGE || end != copy.c_str() + copy.size()) {
    return std::nullopt;
  }
  return value;
#endif
}

// Parses names such as "B12": uppercase column letters, then a row number
// without leading zeros. Returns nullopt for anything else or out of range.
[[nodiscard]] auto parse_cell_name(std::string_view text) -> std::optional<CellRef> {
  std::size_t letters = 0;
  while (letters < text.size() && text[letters] >= 'A' && text[letters] <= 'Z') {
    ++letters;
  }
  const auto digits = text.substr(letters);
  if (letters == 0 || digits.empty() || digits.front() == '0') {
    return std::nullopt;
  }

  // Bijective base 26: A = 1 ... Z = 26, AA = 27; the stored index is 0-based.
  std::uint64_t column = 0;
  for (const char c : text.substr(0, letters)) {
    column = column * 26 + static_cast<std::uint64_t>(c - 'A' + 1);
  }

  std::uint64_t row = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    row = row * 10 + static_cast<std::uint64_t>(c - '0');
    if (row > max_row_number) {
      return std::nullopt;
    }
  }
  return CellRef{Column{static_cast<std::uint32_t>(column - 1)},
                 Row{static_cast<std::uint32_t>(row)}};
}

class DepthGuard {
public:
  explicit DepthGuard(std::size_t& depth) noexcept : depth_{depth} { ++depth_; }
  ~DepthGuard() { --depth_; }
  DepthGuard(const DepthGuard&) = delete;
  auto operator=(const DepthGuard&) -> DepthGuard& = delete;
  DepthGuard(DepthGuard&&) = delete;
  auto operator=(DepthGuard&&) -> DepthGuard& = delete;

private:
  std::size_t& depth_;
};

class Parser {
public:
  Parser(std::span<const Token> tokens, Ast& ast) noexcept : tokens_{tokens}, ast_{&ast} {}

  auto parse_sheet() -> std::expected<void, Diagnostic> {
    while (true) {
      while (check(TokenKind::Newline)) {
        advance();
      }
      if (check(TokenKind::End)) {
        return {};
      }
      if (auto line = parse_definition(); !line) {
        return line;
      }
    }
  }

private:
  using NodeResult = std::expected<NodeId, Diagnostic>;

  [[nodiscard]] auto current() const -> const Token& { return tokens_[position_]; }
  [[nodiscard]] auto peek_kind(std::size_t ahead) const -> TokenKind {
    const auto index = position_ + ahead;
    return index < tokens_.size() ? tokens_[index].kind : TokenKind::End;
  }
  [[nodiscard]] auto check(TokenKind kind) const -> bool { return current().kind == kind; }

  auto advance() -> const Token& {
    const Token& token = current();
    if (token.kind != TokenKind::End) {
      ++position_;
    }
    return token;
  }

  [[nodiscard]] auto error(std::string message) const -> Diagnostic {
    return Diagnostic{Stage::Parse, std::move(message), current().location};
  }

  [[nodiscard]] auto unexpected_token(std::string_view expected) const -> Diagnostic {
    return error(std::format("expected {}, found {}", expected, detail::to_string(current().kind)));
  }

  auto expect(TokenKind kind) -> std::expected<Token, Diagnostic> {
    if (!check(kind)) {
      return std::unexpected(unexpected_token(detail::to_string(kind)));
    }
    return advance();
  }

  auto parse_cell(std::string_view role) -> std::expected<std::pair<CellRef, SourceLocation>, Diagnostic> {
    if (!check(TokenKind::Identifier)) {
      return std::unexpected(unexpected_token(role));
    }
    const Token& token = current();
    const auto ref = parse_cell_name(token.text);
    if (!ref) {
      return std::unexpected(error(std::format("invalid cell reference '{}'", token.text)));
    }
    advance();
    return std::pair{*ref, token.location};
  }

  auto parse_definition() -> std::expected<void, Diagnostic> {
    auto target = parse_cell("a cell name");
    if (!target) {
      return std::unexpected(std::move(target).error());
    }
    if (auto assign = expect(TokenKind::Assign); !assign) {
      return std::unexpected(std::move(assign).error());
    }
    auto expression = parse_expression();
    if (!expression) {
      return std::unexpected(std::move(expression).error());
    }
    if (!check(TokenKind::Newline) && !check(TokenKind::End)) {
      return std::unexpected(unexpected_token("end of line"));
    }
    ast_->add_cell(CellDefinition{target->first, *expression, target->second});
    return {};
  }

  auto parse_expression() -> NodeResult {
    const DepthGuard guard{depth_};
    if (depth_ > max_nesting_depth) {
      return std::unexpected(error("expression is nested too deeply"));
    }
    auto lhs = parse_additive();
    if (!lhs) {
      return lhs;
    }
    const auto op = comparison_op(current().kind);
    if (!op) {
      return lhs;
    }
    const auto location = advance().location;
    auto rhs = parse_additive();
    if (!rhs) {
      return rhs;
    }
    return make(Binary{*op, *lhs, *rhs}, location);
  }

  auto parse_additive() -> NodeResult {
    auto lhs = parse_term();
    while (lhs) {
      const auto op = additive_op(current().kind);
      if (!op) {
        break;
      }
      const auto location = advance().location;
      auto rhs = parse_term();
      if (!rhs) {
        return rhs;
      }
      lhs = make(Binary{*op, *lhs, *rhs}, location);
    }
    return lhs;
  }

  auto parse_term() -> NodeResult {
    auto lhs = parse_unary();
    while (lhs && (check(TokenKind::Star) || check(TokenKind::Slash))) {
      const auto op = check(TokenKind::Star) ? BinaryOp::Mul : BinaryOp::Div;
      const auto location = advance().location;
      auto rhs = parse_unary();
      if (!rhs) {
        return rhs;
      }
      lhs = make(Binary{op, *lhs, *rhs}, location);
    }
    return lhs;
  }

  auto parse_unary() -> NodeResult {
    const DepthGuard guard{depth_};
    if (depth_ > max_nesting_depth) {
      return std::unexpected(error("expression is nested too deeply"));
    }
    if (!check(TokenKind::Minus)) {
      return parse_primary();
    }
    const auto location = advance().location;
    auto operand = parse_unary();
    if (!operand) {
      return operand;
    }
    return make(Negate{*operand}, location);
  }

  auto parse_primary() -> NodeResult {
    const Token& token = current();
    switch (token.kind) {
      case TokenKind::Number: {
        const auto value = parse_number(token.text);
        if (!value) {
          return std::unexpected(error(std::format("invalid number '{}'", token.text)));
        }
        advance();
        return make(NumberLiteral{*value}, token.location);
      }
      case TokenKind::String:
        advance();
        return make(StringLiteral{detail::decode_string(token.text)}, token.location);
      case TokenKind::Plus: {
        auto operand = parse_primary();
        if (!operand) {
          return operand;
        }
        return make(UnaryPlus{*operand}, token.location);
      }
      case TokenKind::LParen: {
        advance();
        auto inner = parse_expression();
        if (!inner) {
          return inner;
        }
        if (auto close = expect(TokenKind::RParen); !close) {
          return std::unexpected(std::move(close).error());
        }
        return inner;
      }
      case TokenKind::Identifier:
        if (peek_kind(1) == TokenKind::LParen) {
          return parse_call();
        }
        if (token.text == "TRUE" || token.text == "FALSE") {
          advance();
          return make(BoolLiteral{token.text == "TRUE"}, token.location);
        }
        return parse_cell_reference();
      default:
        return std::unexpected(unexpected_token("a value"));
    }
  }

  auto parse_cell_reference() -> NodeResult {
    auto cell = parse_cell("a value");
    if (!cell) {
      return std::unexpected(std::move(cell).error());
    }
    return make(CellReference{cell->first}, cell->second);
  }

  auto parse_call() -> NodeResult {
    const Token& name = advance();
    advance();  // '('
    Call call{.name = name.text, .arguments = {}};
    if (!check(TokenKind::RParen)) {
      while (true) {
        auto argument = parse_argument();
        if (!argument) {
          return argument;
        }
        call.arguments.push_back(*argument);
        if (!check(TokenKind::Comma)) {
          break;
        }
        advance();
      }
    }
    if (auto close = expect(TokenKind::RParen); !close) {
      return std::unexpected(std::move(close).error());
    }
    return make(std::move(call), name.location);
  }

  // A range (B1:B9) is only allowed as a function argument.
  auto parse_argument() -> NodeResult {
    if (check(TokenKind::Identifier) && peek_kind(1) == TokenKind::Colon) {
      auto first = parse_cell("a cell name");
      if (!first) {
        return std::unexpected(std::move(first).error());
      }
      advance();  // ':'
      auto last = parse_cell("a cell name");
      if (!last) {
        return std::unexpected(std::move(last).error());
      }
      return make(RangeReference{first->first, last->first}, first->second);
    }
    return parse_expression();
  }

  // Adds a node to the Ast, rejecting trees that grow too tall.
  auto make(Node node, SourceLocation location) -> NodeResult {
    std::uint32_t children = 0;
    const auto child = [&](NodeId id) { children = std::max(children, heights_[id.value]); };
    if (const auto* negate = std::get_if<Negate>(&node)) {
      child(negate->operand);
    } else if (const auto* plus = std::get_if<UnaryPlus>(&node)) {
      child(plus->operand);
    } else if (const auto* binary = std::get_if<Binary>(&node)) {
      child(binary->lhs);
      child(binary->rhs);
    } else if (const auto* call = std::get_if<Call>(&node)) {
      std::ranges::for_each(call->arguments, child);
    }
    const auto height = children + 1;
    if (height > max_tree_height) {
      return std::unexpected(Diagnostic{Stage::Parse, "expression is nested too deeply", location});
    }
    heights_.push_back(height);
    return ast_->add(std::move(node), location);
  }

  [[nodiscard]] static auto comparison_op(TokenKind kind) -> std::optional<BinaryOp> {
    switch (kind) {
      case TokenKind::EqEq: return BinaryOp::Eq;
      case TokenKind::NotEq: return BinaryOp::Ne;
      case TokenKind::Lt: return BinaryOp::Lt;
      case TokenKind::Gt: return BinaryOp::Gt;
      case TokenKind::Le: return BinaryOp::Le;
      case TokenKind::Ge: return BinaryOp::Ge;
      default: return std::nullopt;
    }
  }

  [[nodiscard]] static auto additive_op(TokenKind kind) -> std::optional<BinaryOp> {
    switch (kind) {
      case TokenKind::Plus: return BinaryOp::Add;
      case TokenKind::Minus: return BinaryOp::Sub;
      case TokenKind::Amp: return BinaryOp::Concat;
      default: return std::nullopt;
    }
  }

  std::span<const Token> tokens_;
  std::size_t position_ = 0;
  std::size_t depth_ = 0;
  std::vector<std::uint32_t> heights_;  // by node id
  Ast* ast_;  // never null
};

}  // namespace

auto parse(std::string_view source) -> std::expected<Ast, Diagnostic> {
  Ast ast{std::string{source}};
  // Lex the Ast's own copy so token views stay valid inside the Ast.
  const auto text = ast.source();

  auto body = detail::check_header(text);
  if (!body) {
    return std::unexpected(std::move(body).error());
  }
  auto tokens = detail::lex(text, *body);
  if (!tokens) {
    return std::unexpected(std::move(tokens).error());
  }
  if (auto parsed = Parser{*tokens, ast}.parse_sheet(); !parsed) {
    return std::unexpected(std::move(parsed).error());
  }
  return ast;
}

}  // namespace sheet
