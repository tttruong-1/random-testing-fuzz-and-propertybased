#include "lexer.hpp"

#include <format>
#include <string>
#include <utility>

namespace sheet::detail {
namespace {

[[nodiscard]] constexpr auto is_digit(char c) noexcept -> bool { return c >= '0' && c <= '9'; }

[[nodiscard]] constexpr auto is_alpha(char c) noexcept -> bool {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] constexpr auto is_identifier_char(char c) noexcept -> bool {
  return is_alpha(c) || is_digit(c) || c == '_';
}

class Lexer {
public:
  Lexer(std::string_view source, std::size_t offset) noexcept
      : source_{source}, position_{offset} {}

  auto run() -> std::expected<std::vector<Token>, Diagnostic> {
    while (!at_end()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\r') {
        advance();
      } else if (c == '#') {
        skip_comment();
      } else if (c == '\n') {
        emit(TokenKind::Newline, 1);
      } else if (is_digit(c)) {
        lex_number();
      } else if (is_alpha(c)) {
        lex_identifier();
      } else if (c == '"') {
        if (auto status = lex_string(); !status) {
          return std::unexpected(std::move(status).error());
        }
      } else if (auto status = lex_punctuation(); !status) {
        return std::unexpected(std::move(status).error());
      }
    }
    tokens_.push_back(Token{TokenKind::End, source_.substr(position_, 0), location_});
    return std::move(tokens_);
  }

private:
  [[nodiscard]] auto at_end() const noexcept -> bool { return position_ >= source_.size(); }
  [[nodiscard]] auto peek() const -> char { return source_[position_]; }
  [[nodiscard]] auto peek_next() const -> char {
    return position_ + 1 < source_.size() ? source_[position_ + 1] : '\0';
  }

  auto advance() -> void {
    if (source_[position_] == '\n') {
      ++location_.line;
      location_.column = 1;
    } else {
      ++location_.column;
    }
    ++position_;
  }

  auto emit(TokenKind kind, std::size_t length) -> void {
    tokens_.push_back(Token{kind, source_.substr(position_, length), location_});
    for (std::size_t i = 0; i < length; ++i) {
      advance();
    }
  }

  [[nodiscard]] auto error(std::string message) const -> Diagnostic {
    return Diagnostic{Stage::Lex, std::move(message), location_};
  }

  auto skip_comment() -> void {
    while (!at_end() && peek() != '\n') {
      advance();
    }
  }

  auto lex_number() -> void {
    auto end = position_;
    const auto digits = [&] {
      while (end < source_.size() && is_digit(source_[end])) {
        ++end;
      }
    };
    digits();
    if (end + 1 < source_.size() && source_[end] == '.' && is_digit(source_[end + 1])) {
      ++end;
      digits();
    }
    if (end < source_.size() && (source_[end] == 'e' || source_[end] == 'E')) {
      auto exponent = end + 1;
      if (exponent < source_.size() && (source_[exponent] == '+' || source_[exponent] == '-')) {
        ++exponent;
      }
      if (exponent < source_.size() && is_digit(source_[exponent])) {
        end = exponent;
        digits();
      }
    }
    emit(TokenKind::Number, end - position_);
  }

  auto lex_identifier() -> void {
    auto end = position_;
    while (end < source_.size() && is_identifier_char(source_[end])) {
      ++end;
    }
    emit(TokenKind::Identifier, end - position_);
  }

  auto lex_string() -> std::expected<void, Diagnostic> {
    auto end = position_ + 1;  // past the opening quote
    while (end < source_.size()) {
      const char c = source_[end];
      if (c == '"') {
        emit(TokenKind::String, end + 1 - position_);
        return {};
      }
      if (c == '\n') {
        break;
      }
      if (c == '\\') {
        const char escaped = source_[end + 1];
        if (escaped != '"' && escaped != '\\' && escaped != 'n' && escaped != 't') {
          return std::unexpected(error(std::format("unknown escape '\\{}'", escaped)));
        }
        end += 2;
        continue;
      }
      ++end;
    }
    return std::unexpected(error("unterminated string literal"));
  }

  auto lex_punctuation() -> std::expected<void, Diagnostic> {
    const char c = peek();
    const char next = peek_next();
    switch (c) {
      case ':': emit(TokenKind::Colon, 1); return {};
      case ',': emit(TokenKind::Comma, 1); return {};
      case '(': emit(TokenKind::LParen, 1); return {};
      case ')': emit(TokenKind::RParen, 1); return {};
      case '+': emit(TokenKind::Plus, 1); return {};
      case '-': emit(TokenKind::Minus, 1); return {};
      case '*': emit(TokenKind::Star, 1); return {};
      case '/': emit(TokenKind::Slash, 1); return {};
      case '&': emit(TokenKind::Amp, 1); return {};
      case '=':
        next == '=' ? emit(TokenKind::EqEq, 2) : emit(TokenKind::Assign, 1);
        return {};
      case '<':
        if (next == '>') {
          emit(TokenKind::NotEq, 2);
        } else {
          next == '=' ? emit(TokenKind::Le, 2) : emit(TokenKind::Lt, 1);
        }
        return {};
      case '>':
        next == '=' ? emit(TokenKind::Ge, 2) : emit(TokenKind::Gt, 1);
        return {};
      default: break;
    }
    const auto byte = static_cast<unsigned>(static_cast<unsigned char>(c));
    return std::unexpected(error(std::format("unexpected character (byte 0x{:02x})", byte)));
  }

  std::string_view source_;
  std::size_t position_;
  SourceLocation location_{.line = 2, .column = 1};
  std::vector<Token> tokens_;
};

}  // namespace

auto to_string(TokenKind kind) -> std::string_view {
  switch (kind) {
    case TokenKind::Number: return "number";
    case TokenKind::String: return "string";
    case TokenKind::Identifier: return "name";
    case TokenKind::Colon: return "':'";
    case TokenKind::Comma: return "','";
    case TokenKind::LParen: return "'('";
    case TokenKind::RParen: return "')'";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::Amp: return "'&'";
    case TokenKind::Assign: return "'='";
    case TokenKind::EqEq: return "'=='";
    case TokenKind::NotEq: return "'<>'";
    case TokenKind::Lt: return "'<'";
    case TokenKind::Gt: return "'>'";
    case TokenKind::Le: return "'<='";
    case TokenKind::Ge: return "'>='";
    case TokenKind::Newline: return "end of line";
    case TokenKind::End: return "end of input";
  }
  return "token";
}

auto check_header(std::string_view source) -> std::expected<std::size_t, Diagnostic> {
  const auto newline = source.find('\n');
  auto first_line = source.substr(0, newline);
  if (first_line.ends_with('\r')) {
    first_line.remove_suffix(1);
  }
  if (first_line != header_line) {
    return std::unexpected(Diagnostic{
        Stage::Header, std::format("expected '{}' on the first line", header_line), {}});
  }
  return newline == std::string_view::npos ? source.size() : newline + 1;
}

auto lex(std::string_view source, std::size_t offset)
    -> std::expected<std::vector<Token>, Diagnostic> {
  return Lexer{source, offset}.run();
}

auto decode_string(std::string_view quoted) -> std::string {
  // The lexer guarantees the quotes and that every backslash starts a
  // complete, known escape.
  const auto body = quoted.substr(1, quoted.size() - 2);
  std::string decoded;
  decoded.reserve(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    if (body[i] != '\\') {
      decoded.push_back(body[i]);
      continue;
    }
    ++i;
    switch (body[i]) {
      case 'n': decoded.push_back('\n'); break;
      case 't': decoded.push_back('\t'); break;
      default: decoded.push_back(body[i]); break;
    }
  }
  return decoded;
}

}  // namespace sheet::detail
