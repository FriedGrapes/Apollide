// Apollide: standalone test for kiln_monitor_math.h (g++ -std=c++20 -I. tests/apollide/test_kiln_monitor_math.cpp)
#include "src/apollide/kiln_monitor_math.h"

#include <cstdio>

static int failures = 0;

static void expect(bool got, const char *what) {
  if (!got) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

int main() {
  using namespace apollide::kiln_monitor;

  expect(mode_valid({1920, 1080, 30}), "full HD at 30 is fine");
  expect(mode_valid({2560, 1440, 60}), "QHD at 60 is fine");
  expect(!mode_valid({0, 1080, 30}), "zero width is refused");
  expect(!mode_valid({1921, 1080, 30}), "odd width is refused");
  expect(!mode_valid({1920, 1080, 0}), "zero fps is refused");
  expect(!mode_valid({16000, 1080, 30}), "absurd width is refused");
  expect(!mode_valid({1920, 1080, 500}), "absurd fps is refused");

  expect(request_allowed(true, true), "this PC with a token");
  expect(!request_allowed(false, true), "another device even with a token");
  expect(!request_allowed(true, false), "this PC without a token");

  using topo_t = std::vector<std::vector<std::string>>;
  expect(exclusive_topology("", "K") == topo_t {}, "no stream display: leave the default");
  expect(exclusive_topology("U", "") == topo_t {{"U"}}, "no Kiln monitor: only the stream display");
  expect(exclusive_topology("U", "K") == (topo_t {{"U"}, {"K"}}), "Kiln monitor stays as its own extended group");
  expect(exclusive_topology("K", "K") == topo_t {{"K"}}, "streaming the Kiln monitor itself: one group");

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("ok\n");
  return 0;
}
