/**
 * @file src/apollide/release_sums.h
 * @brief Apollide: read a SHA256SUMS file (header only, so it is testable on its own).
 */
#pragma once

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace apollide::auto_update {
  /// SHA-256 (lowercase hex) for `file_name` in a SHA256SUMS text ("<hash>  <name>" or "<hash> *<name>"), or empty.
  inline std::string expected_sha256(std::string_view sums, std::string_view file_name) {
    std::istringstream lines {std::string(sums)};
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.size() < 66 || line[64] != ' ') {
        continue;
      }
      bool hex = true;
      for (size_t i = 0; i < 64; ++i) {
        hex = hex && std::isxdigit(static_cast<unsigned char>(line[i]));
      }
      auto name = std::string_view(line).substr(65);
      if (!name.empty() && (name.front() == ' ' || name.front() == '*')) {
        name.remove_prefix(1);
      }
      if (hex && name == file_name) {
        std::string hash = line.substr(0, 64);
        for (auto &c : hash) {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return hash;
      }
    }
    return {};
  }
}  // namespace apollide::auto_update
