#include <string>
#include <string_view>
#include <variant>

#include "check.hpp"
#include "sheet/sheet.hpp"

namespace {

auto stage_of(std::string_view source) -> sheet::Stage {
  const auto ast = sheet::parse(source);
  return ast ? sheet::Stage::Done : ast.error().stage;
}

}  // namespace

TEST_CASE("header is required") {
  CHECK(stage_of("") == sheet::Stage::Header);
  CHECK(stage_of("#sheet v2\nA1 = 1\n") == sheet::Stage::Header);
  CHECK(stage_of("A1 = 1\n") == sheet::Stage::Header);
  CHECK(stage_of("#sheet v1") == sheet::Stage::Done);
  CHECK(stage_of("#sheet v1\r\nA1 = 1\r\n") == sheet::Stage::Done);
}

TEST_CASE("lexer rejects bad characters and strings") {
  CHECK(stage_of("#sheet v1\nA1 = 1 $ 2\n") == sheet::Stage::Lex);
  CHECK(stage_of("#sheet v1\nA1 = \"open\n") == sheet::Stage::Lex);
  CHECK(stage_of("#sheet v1\nA1 = \"bad \\q escape\"\n") == sheet::Stage::Lex);
}

TEST_CASE("parser accepts the example sheet") {
  constexpr std::string_view source =
      "#sheet v1\n"
      "A1 = 10\n"
      "A2 = A1 * 2\n"
      "# comment line\n"
      "B1 = SUM(A1:A2)\n"
      "B2 = IF(B1 > 25, \"big\", CONCAT(\"small: \", B1))\n"
      "C1 = B1 / (A2 - 20)\n";
  const auto ast = sheet::parse(source);
  CHECK(ast.has_value());
  if (ast) {
    CHECK(ast->cells().size() == 5);
    CHECK(ast->cells()[2].target == (sheet::CellRef{sheet::Column{1}, sheet::Row{1}}));
  }
}

TEST_CASE("parser respects precedence") {
  const auto ast = sheet::parse("#sheet v1\nA1 = 1 + 2 * 3\n");
  CHECK(ast.has_value());
  if (!ast) {
    return;
  }
  const auto& root = ast->node(ast->cells()[0].expression);
  const auto* add = std::get_if<sheet::Binary>(&root);
  CHECK(add != nullptr && add->op == sheet::BinaryOp::Add);
  if (add != nullptr) {
    const auto* mul = std::get_if<sheet::Binary>(&ast->node(add->rhs));
    CHECK(mul != nullptr && mul->op == sheet::BinaryOp::Mul);
  }
}

TEST_CASE("parser decodes string escapes") {
  const auto ast = sheet::parse("#sheet v1\nA1 = \"a\\\"b\\\\c\\n\"\n");
  CHECK(ast.has_value());
  if (ast) {
    const auto* text = std::get_if<sheet::StringLiteral>(&ast->node(ast->cells()[0].expression));
    CHECK(text != nullptr && text->value == "a\"b\\c\n");
  }
}

TEST_CASE("parser rejects malformed definitions") {
  CHECK(stage_of("#sheet v1\nA1 = \n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 10\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA0 = 1\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 = (1 + 2\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 = B1:B2\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 = 1 2\n") == sheet::Stage::Parse);
}

TEST_CASE("nesting depth is limited, not a stack overflow") {
  const std::string open(5000, '(');
  const std::string close(5000, ')');
  CHECK(stage_of("#sheet v1\nA1 = " + open + "1" + close + "\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 = " + std::string(5000, '-') + "1\n") == sheet::Stage::Parse);
  CHECK(stage_of("#sheet v1\nA1 = " + std::string(100, '(') + "1" + std::string(100, ')') +
                 "\n") == sheet::Stage::Done);
}

TEST_CASE("the Ast keeps its source alive across moves") {
  auto ast = sheet::parse("#sheet v1\nA1 = SUM(1)\n");
  CHECK(ast.has_value());
  if (!ast) {
    return;
  }
  const sheet::Ast moved = std::move(*ast);
  const auto* call = std::get_if<sheet::Call>(&moved.node(moved.cells()[0].expression));
  CHECK(call != nullptr && call->name == "SUM");
}
