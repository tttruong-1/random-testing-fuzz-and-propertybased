// println for the tools and tests, built on std::format.
// std::println needs GCC 14's libstdc++; this keeps GCC 12 and 13 working.
#pragma once

#include <cstdio>
#include <format>
#include <string>
#include <utility>

namespace sheet {

template <class... Args>
void println(std::FILE* stream, std::format_string<Args...> format, Args&&... args) {
  std::string line = std::format(format, std::forward<Args>(args)...);
  line.push_back('\n');
  std::fputs(line.c_str(), stream);
}

template <class... Args>
void println(std::format_string<Args...> format, Args&&... args) {
  println(stdout, format, std::forward<Args>(args)...);
}

}  // namespace sheet
