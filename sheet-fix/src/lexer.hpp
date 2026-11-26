#pragma once

// Internal: the header check and the lexer.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "sheet/types.hpp"

namespace sheet::detail {

inline constexpr std::string_view header_line = "#sheet v1";

enum class TokenKind : std::uint8_t {
  Number,
  String,      // text includes the surrounding quotes; escapes not yet decoded
  Identifier,  // cell names, function names, TRUE and FALSE
  Colon, Comma, LParen, RParen,
  Plus, Minus, Star, Slash, Amp,
  Assign,  // =
  EqEq, NotEq, Lt, Gt, Le, Ge,
  Newline,
  End,
};

[[nodiscard]] auto to_string(TokenKind kind) -> std::string_view;

// A token's text is a view into the source passed to lex(); it is valid only
// as long as that source is.
struct Token {
  TokenKind kind;
  std::string_view text;
  SourceLocation location;
};

// Checks the "#sheet v1" line. On success, returns the offset of the first
// byte after the header's newline.
[[nodiscard]] auto check_header(std::string_view source) -> std::expected<std::size_t, Diagnostic>;

// Lexes the body of a sheet, starting at `offset` (on line 2). The result
// always ends with a single End token.
[[nodiscard]] auto lex(std::string_view source, std::size_t offset)
    -> std::expected<std::vector<Token>, Diagnostic>;

// Decodes the escapes in a String token's text (\" \\ \n \t).
[[nodiscard]] auto decode_string(std::string_view quoted) -> std::string;

}  // namespace sheet::detail
