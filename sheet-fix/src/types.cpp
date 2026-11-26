#include "sheet/types.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace sheet {
namespace {

template <typename... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};

}  // namespace

auto to_string(CellRef ref) -> std::string {
  // Bijective base 26: index 0 = A, 25 = Z, 26 = AA.
  std::string letters;
  for (auto n = ref.column.index() + 1; n > 0; n = (n - 1) / 26) {
    letters.push_back(static_cast<char>('A' + (n - 1) % 26));
  }
  std::ranges::reverse(letters);
  return std::format("{}{}", letters, ref.row.number());
}

auto to_string(ErrorValue error) -> std::string_view {
  switch (error.kind) {
    case ErrorKind::DivZero: return "#DIV/0";
    case ErrorKind::Ref: return "#REF";
    case ErrorKind::Value: return "#VALUE";
    case ErrorKind::Num: return "#NUM";
  }
  return "#ERROR";
}

auto to_string(const Value& value) -> std::string {
  return std::visit(
      Overloaded{
          [](double number) { return std::format("{}", number); },
          [](const std::string& text) {
            std::string quoted{"\""};
            for (const char c : text) {
              switch (c) {
                case '"': quoted += "\\\""; break;
                case '\\': quoted += "\\\\"; break;
                case '\n': quoted += "\\n"; break;
                case '\t': quoted += "\\t"; break;
                default: quoted.push_back(c); break;
              }
            }
            return quoted + "\"";
          },
          [](bool flag) { return std::string{flag ? "TRUE" : "FALSE"}; },
          [](ErrorValue error) { return std::string{to_string(error)}; },
      },
      value);
}

}  // namespace sheet
