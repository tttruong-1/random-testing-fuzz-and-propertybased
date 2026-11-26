// libFuzzer harness for the sheet library.
//
// TODO(students): implement LLVMFuzzerTestOneInput.
//
// libFuzzer calls this function with arbitrary bytes, over and over, in one
// process. Use the public API in sheet/sheet.hpp to push those bytes as far
// through the pipeline as you can. Things to consider:
//
//   * Which API calls does your harness make? A stage your harness never
//     calls is a stage the fuzzer can never explore.
//   * What should happen when a stage rejects the input?
//   * The harness must not leak or keep state between calls.
//
// Build with:   cmake --preset fuzz && cmake --build --preset fuzz
// Smoke test:   build/fuzz/fuzz_sheet -runs=1000 seeds/

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sheet/sheet.hpp"

// The entry point name is fixed by libFuzzer.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  [[maybe_unused]] const std::string_view source{reinterpret_cast<const char*>(data), size};

  // TODO(students): call into the sheet library here.
  [[maybe_unused]] const auto result = sheet::parse(source)
                                           .and_then(sheet::check)
                                           .and_then(sheet::evaluate);

  return 0;
}
