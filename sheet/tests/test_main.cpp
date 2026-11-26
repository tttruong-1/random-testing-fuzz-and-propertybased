// Runs every registered test. With --seeds DIR, instead checks that every
// seed in DIR parses (the seed corpus must stay valid).

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "sheet/print.hpp"
#include <span>
#include <string>
#include <string_view>

#include "check.hpp"
#include "sheet/sheet.hpp"

namespace {

auto check_seeds(const std::filesystem::path& directory) -> int {
  int bad = 0;
  int total = 0;
  for (const auto& entry : std::filesystem::directory_iterator{directory}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    ++total;
    std::ifstream in{entry.path(), std::ios::binary};
    const std::string source{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (auto ast = sheet::parse(source); !ast) {
      ++bad;
      const auto& d = ast.error();
      sheet::println(stderr, "{}: rejected at {} ({}:{}): {}", entry.path().filename().string(),
                   to_string(d.stage), d.location.line, d.location.column, d.message);
    }
  }
  sheet::println("{} of {} seeds parse", total - bad, total);
  return bad == 0 && total > 0 ? 0 : 1;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const auto args = std::span{argv, static_cast<std::size_t>(argc)}.subspan(1);
  if (args.size() == 2 && std::string_view{args[0]} == "--seeds") {
    return check_seeds(args[1]);
  }

  for (const auto& test : sheet::test::registry()) {
    const int before = sheet::test::failures();
    test.body();
    sheet::println("{} {}", sheet::test::failures() == before ? "PASS" : "FAIL", test.name);
  }
  const int failed = sheet::test::failures();
  sheet::println("{} checks failed", failed);
  return failed == 0 ? 0 : 1;
}
