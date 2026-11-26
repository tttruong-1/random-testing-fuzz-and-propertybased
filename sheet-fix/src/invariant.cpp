#include "invariant.hpp"

#include <cstdlib>
#include "sheet/print.hpp"

namespace sheet::detail {

auto invariant_failure(std::string_view message, std::source_location where) -> void {
  sheet::println(stderr, "sheet: invariant violated at {}:{}: {}", where.file_name(), where.line(),
               message);
  std::abort();
}

}  // namespace sheet::detail
