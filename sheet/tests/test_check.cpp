#include <format>
#include <string>
#include <string_view>

#include "check.hpp"
#include "sheet/sheet.hpp"

namespace {

auto stage_of(std::string_view body) -> sheet::Stage {
  const auto result = sheet::parse(std::string{"#sheet v1\n"} + std::string{body})
                          .and_then(sheet::check);
  return result ? sheet::Stage::Done : result.error().stage;
}

}  // namespace

TEST_CASE("resolve rejects duplicates, undefined cells and bad calls") {
  CHECK(stage_of("A1 = 1\nA1 = 2\n") == sheet::Stage::Resolve);
  CHECK(stage_of("A1 = B7\n") == sheet::Stage::Resolve);
  CHECK(stage_of("A1 = NOPE(1)\n") == sheet::Stage::Resolve);
  CHECK(stage_of("A1 = IF(TRUE, 1)\n") == sheet::Stage::Resolve);
  CHECK(stage_of("A1 = LEN(\"a\", \"b\")\n") == sheet::Stage::Resolve);
  CHECK(stage_of("A1 = SUM()\n") == sheet::Stage::Resolve);
}

TEST_CASE("resolve allows ranges over undefined cells and forward references") {
  CHECK(stage_of("A1 = SUM(Z1:Z9)\n") == sheet::Stage::Done);
  CHECK(stage_of("A1 = B1 + 1\nB1 = 2\n") == sheet::Stage::Done);
}

TEST_CASE("dependencies reject cycles, including through ranges") {
  CHECK(stage_of("A1 = A1\n") == sheet::Stage::Dependencies);
  CHECK(stage_of("A1 = B1\nB1 = A1\n") == sheet::Stage::Dependencies);
  CHECK(stage_of("B1 = SUM(A1:A2)\nA1 = 1\nA2 = B1\n") == sheet::Stage::Dependencies);
  CHECK(stage_of("A3 = 1\nB1 = COUNT(A3:A1)\n") == sheet::Stage::Done);
}

TEST_CASE("types reject mismatched operands") {
  CHECK(stage_of("A1 = \"a\" + 1\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = -TRUE\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = 1 == \"1\"\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = TRUE < FALSE\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = IF(1, 2, 3)\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = IF(TRUE, 2, \"3\")\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = LEN(5)\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = 1\nB1 = ROUND(A1:A1, 2)\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = 1\nB1 = IF(TRUE, A1:A1, 2)\n") == sheet::Stage::Types);
}

TEST_CASE("types check every cell a numeric range covers, either way round") {
  CHECK(stage_of("A1 = 1\nA2 = \"x\"\nB1 = SUM(A1:A2)\n") == sheet::Stage::Types);
  CHECK(stage_of("A1 = 1\nA2 = \"x\"\nB1 = COUNT(A2:A1)\n") == sheet::Stage::Done);
  CHECK(stage_of("A1 = 1\nA2 = \"x\"\nB1 = CONCAT(A1:A2)\n") == sheet::Stage::Done);
}

TEST_CASE("types accept well-typed sheets") {
  CHECK(stage_of("A1 = \"a\" & 1 & TRUE\n") == sheet::Stage::Done);
  CHECK(stage_of("A1 = \"a\" < \"b\"\n") == sheet::Stage::Done);
  CHECK(stage_of("A1 = TRUE == FALSE\n") == sheet::Stage::Done);
  CHECK(stage_of("A1 = IF(1 > 2, \"x\", \"y\")\nB1 = LEN(A1)\n") == sheet::Stage::Done);
}
