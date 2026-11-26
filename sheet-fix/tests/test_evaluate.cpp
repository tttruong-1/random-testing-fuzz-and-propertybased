#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "check.hpp"
#include "sheet/sheet.hpp"

namespace {

// Evaluates a sheet body and returns the printed value of `cell`, or the
// rejecting stage's name in angle brackets.
auto value_of(std::string_view body, std::string_view cell) -> std::string {
  const auto result = sheet::parse(std::string{"#sheet v1\n"} + std::string{body})
                          .and_then(sheet::check)
                          .and_then(sheet::evaluate);
  if (!result) {
    return std::format("<{}>", to_string(result.error().stage));
  }
  for (const auto& [ref, value] : result->all()) {
    if (to_string(ref) == cell) {
      return to_string(value);
    }
  }
  return "<missing>";
}

}  // namespace

TEST_CASE("arithmetic and precedence") {
  CHECK(value_of("A1 = 1 + 2 * 3 - 4 / 2\n", "A1") == "5");
  CHECK(value_of("A1 = -(2 + 3)\n", "A1") == "-5");
  CHECK(value_of("A1 = 2\nB1 = A1 * A1\n", "B1") == "4");
}

TEST_CASE("errors are values that propagate") {
  CHECK(value_of("A1 = 1 / 0\nB1 = A1 + 1\nC1 = SUM(A1:B1)\n", "C1") == "#DIV/0");
  CHECK(value_of("A1 = 1e308 * 10\n", "A1") == "#NUM");
  CHECK(value_of("A1 = 1 / 0\nB1 = A1 & \"x\"\n", "B1") == "#DIV/0");
  CHECK(value_of("A1 = 1 / 0\nB1 = COUNT(A1, 2)\n", "B1") == "1");
}

TEST_CASE("forward references evaluate in dependency order") {
  CHECK(value_of("A1 = B1 + 1\nB1 = C1 * 2\nC1 = 5\n", "A1") == "11");
}

TEST_CASE("aggregates") {
  constexpr std::string_view body = "A1 = 1\nA2 = 2\nA3 = 6\n";
  CHECK(value_of(std::string{body} + "B1 = SUM(A1:A3, 10)\n", "B1") == "19");
  CHECK(value_of(std::string{body} + "B1 = AVERAGE(A3:A1)\n", "B1") == "3");
  CHECK(value_of(std::string{body} + "B1 = MIN(A1:A3)\n", "B1") == "1");
  CHECK(value_of(std::string{body} + "B1 = MAX(A1:A3)\n", "B1") == "6");
  CHECK(value_of(std::string{body} + "B1 = COUNT(A1:A9)\n", "B1") == "3");
  CHECK(value_of("B1 = AVERAGE(Z1:Z3)\n", "B1") == "#DIV/0");
  CHECK(value_of("B1 = MAX(Z1:Z3)\n", "B1") == "0");
}

TEST_CASE("IF evaluates only the chosen branch") {
  CHECK(value_of("A1 = IF(TRUE, 1, 1 / 0)\n", "A1") == "1");
  CHECK(value_of("A1 = IF(2 < 1, \"a\", \"b\")\n", "A1") == "\"b\"");
}

TEST_CASE("text functions and comparisons") {
  CHECK(value_of("A1 = \"n=\" & 2.5 & \" \" & TRUE\n", "A1") == "\"n=2.5 TRUE\"");
  CHECK(value_of("A1 = LEN(\"hello\")\n", "A1") == "5");
  CHECK(value_of("A1 = \"apple\" < \"banana\"\n", "A1") == "TRUE");
  CHECK(value_of("A1 = 3 >= 3\n", "A1") == "TRUE");
}

TEST_CASE("ROUND") {
  CHECK(value_of("A1 = ROUND(2.375, 2)\n", "A1") == "2.38");
  CHECK(value_of("A1 = ROUND(1250, -2)\n", "A1") == "1300");
  CHECK(value_of("A1 = ROUND(1.5, 0.5)\n", "A1") == "#NUM");
  CHECK(value_of("A1 = ROUND(1.5, 99)\n", "A1") == "#NUM");
}

TEST_CASE("tall expression trees are rejected while parsing") {
  std::string chain = "A1 = 1";
  for (int i = 0; i < 300; ++i) {
    chain += " + 1";
  }
  CHECK(value_of(chain + "\n", "A1") == "<parse>");
  std::string shorter = "A1 = 1";
  for (int i = 0; i < 200; ++i) {
    shorter += " + 1";
  }
  CHECK(value_of(shorter + "\n", "A1") == "201");
}
