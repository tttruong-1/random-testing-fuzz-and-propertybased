#include <string>

#include "check.hpp"
#include "sheet/types.hpp"

TEST_CASE("cell references print in spreadsheet notation") {
  using sheet::CellRef;
  using sheet::Column;
  using sheet::Row;
  CHECK(to_string(CellRef{Column{0}, Row{1}}) == "A1");
  CHECK(to_string(CellRef{Column{25}, Row{9}}) == "Z9");
  CHECK(to_string(CellRef{Column{26}, Row{10}}) == "AA10");
  CHECK(to_string(CellRef{Column{sheet::max_column_index}, Row{3}}) == "XFD3");
}

TEST_CASE("cell references order by column, then row") {
  using sheet::CellRef;
  using sheet::Column;
  using sheet::Row;
  CHECK((CellRef{Column{0}, Row{9}} < CellRef{Column{1}, Row{1}}));
  CHECK((CellRef{Column{2}, Row{1}} < CellRef{Column{2}, Row{2}}));
}

TEST_CASE("values print") {
  CHECK(to_string(sheet::Value{2.5}) == "2.5");
  CHECK(to_string(sheet::Value{std::string{"hi"}}) == "\"hi\"");
  CHECK(to_string(sheet::Value{true}) == "TRUE");
  CHECK(to_string(sheet::Value{sheet::ErrorValue{sheet::ErrorKind::DivZero}}) == "#DIV/0");
}
