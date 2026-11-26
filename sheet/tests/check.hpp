#pragma once

// A deliberately tiny test framework, so the repository builds with no
// downloads. Tests register themselves; CHECK records failures and continues.

#include <functional>
#include "sheet/print.hpp"
#include <source_location>
#include <string_view>
#include <vector>

namespace sheet::test {

struct TestCase {
  std::string_view name;
  std::function<void()> body;
};

inline auto registry() -> std::vector<TestCase>& {
  static std::vector<TestCase> tests;
  return tests;
}

inline auto failures() -> int& {
  static int count = 0;
  return count;
}

struct Register {
  Register(std::string_view name, std::function<void()> body) {
    registry().push_back(TestCase{name, std::move(body)});
  }
};

inline auto check(bool condition, std::string_view expression,
                  std::source_location where = std::source_location::current()) -> bool {
  if (!condition) {
    ++failures();
    sheet::println(stderr, "  {}:{}: CHECK failed: {}", where.file_name(), where.line(), expression);
  }
  return condition;
}

}  // namespace sheet::test

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define SHEET_CONCAT_IMPL(a, b) a##b
#define SHEET_CONCAT(a, b) SHEET_CONCAT_IMPL(a, b)
#define TEST_CASE(name)                                                                  \
  static void SHEET_CONCAT(sheet_test_, __LINE__)();                                     \
  static const sheet::test::Register SHEET_CONCAT(sheet_register_, __LINE__){            \
      name, &SHEET_CONCAT(sheet_test_, __LINE__)};                                       \
  static void SHEET_CONCAT(sheet_test_, __LINE__)()
#define CHECK(expression) sheet::test::check(static_cast<bool>(expression), #expression)
// NOLINTEND(cppcoreguidelines-macro-usage)
