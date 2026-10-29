// Apollide: standalone test for release_sums.h (g++ -std=c++20 -I. tests/apollide/test_release_sums.cpp)
#include "src/apollide/release_sums.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(const std::string &got, const std::string &want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got '%s' want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
  }
}

int main() {
  using apollide::auto_update::expected_sha256;
  const std::string a(64, 'a');
  const std::string B = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
  const std::string sums = a + "  Apollide_v0.2.1.msi\n" + B + " *Apollide_v0.2.1-portable.zip\r\n";

  expect(expected_sha256(sums, "Apollide_v0.2.1.msi"), a, "two-space line");
  expect(expected_sha256(sums, "Apollide_v0.2.1-portable.zip"), "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "binary marker, CRLF, lowercase");
  expect(expected_sha256(sums, "Apollide_v0.2.0.msi"), "", "missing name");
  expect(expected_sha256(sums, "Apollide_v0.2.1"), "", "prefix of a name");
  expect(expected_sha256("zz" + a.substr(2) + "  x.msi\n", "x.msi"), "", "not hex");
  expect(expected_sha256(a.substr(1) + "  x.msi\n", "x.msi"), "", "short hash");
  expect(expected_sha256("", "x.msi"), "", "empty file");

  std::printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
