cmake_minimum_required(VERSION 3.28)
project(sheet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules, so skip the dependency scan (it needs clang-scan-deps).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Sanitizer and coverage flags come from CMakePresets.json
# through CMAKE_CXX_FLAGS so that every target in a build agrees on them.
option(SHEET_WERROR "Treat warnings as errors" ON)
option(SHEET_BUILD_FUZZER "Build the libFuzzer harness" OFF)
option(SHEET_BUILD_COVTRACE "Build covtrace (instruments a separate copy of the library)" OFF)
option(SHEET_BUILD_TESTS "Build unit tests" ON)
option(SHEET_HARDENING
    "Standard library hardening: _GLIBCXX_ASSERTIONS bounds-checks string_view, vector, optional and more" ON)
set(SHEET_FUZZER_EXTRA_LINK "" CACHE STRING
    "Extra link items for the fuzzer runtime; only for toolchains whose libFuzzer needs them")

# ---------------------------------------------------------------------------
# Shared settings

add_library(sheet_settings INTERFACE)
target_compile_options(sheet_settings INTERFACE
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
  -Wnon-virtual-dtor -Wold-style-cast -Wimplicit-fallthrough
  $<$<BOOL:${SHEET_WERROR}>:-Werror>)
target_compile_definitions(sheet_settings INTERFACE
  $<$<BOOL:${SHEET_HARDENING}>:_GLIBCXX_ASSERTIONS>)

# ---------------------------------------------------------------------------
# The library

set(SHEET_SOURCES
  src/ast.cpp
  src/cell_range.cpp
  src/check.cpp
  src/dependencies.cpp
  src/evaluate.cpp
  src/invariant.cpp
  src/lexer.cpp
  src/parser.cpp
  src/resolve.cpp
  src/type_check.cpp
  src/types.cpp)

function(sheet_add_library name)
  add_library(${name} STATIC ${SHEET_SOURCES})
  target_include_directories(${name} PUBLIC include PRIVATE src)
  target_link_libraries(${name} PUBLIC sheet_settings)
endfunction()

sheet_add_library(sheet)

# ---------------------------------------------------------------------------
# Tools

add_executable(sheet-stages tools/sheet_stages.cpp)
target_link_libraries(sheet-stages PRIVATE sheet)

add_executable(sheet-repro tools/sheet_repro.cpp)
target_link_libraries(sheet-repro PRIVATE sheet)

if(SHEET_BUILD_COVTRACE)
  # A second copy of the library, instrumented for edge tracing. Only the
  # library is instrumented, so the edge count measures the target alone.
  sheet_add_library(sheet_covtrace)
  target_compile_options(sheet_covtrace PRIVATE -fsanitize-coverage=trace-pc-guard)

  add_executable(covtrace tools/covtrace.cpp tools/covtrace_runtime.cpp)
  target_link_libraries(covtrace PRIVATE sheet_covtrace)
endif()

# ---------------------------------------------------------------------------
# Fuzzing

if(SHEET_BUILD_FUZZER)
  # The preset compiles everything with -fsanitize=fuzzer-no-link; only the
  # harness executable links the fuzzer runtime and its main().
  add_executable(fuzz_sheet harness/fuzz_sheet.cpp)
  target_link_libraries(fuzz_sheet PRIVATE sheet ${SHEET_FUZZER_EXTRA_LINK})
  target_link_options(fuzz_sheet PRIVATE -fsanitize=fuzzer)

endif()

# ---------------------------------------------------------------------------
# Tests

if(SHEET_BUILD_TESTS)
  enable_testing()
  add_executable(sheet_tests
    tests/test_main.cpp tests/test_parse.cpp tests/test_types.cpp
    tests/test_check.cpp tests/test_evaluate.cpp
    )
  target_include_directories(sheet_tests PRIVATE src tests)
  target_link_libraries(sheet_tests PRIVATE sheet)
  add_test(NAME unit COMMAND sheet_tests)
  add_test(NAME seeds-parse
           COMMAND sheet_tests --seeds ${CMAKE_CURRENT_SOURCE_DIR}/seeds)
endif()
