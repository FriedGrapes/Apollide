// Apollide: standalone test for display_seat_math.h (g++ -std=c++20 -I. tests/apollide/test_display_seat_math.cpp)
#include "src/apollide/display_seat_math.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

static void expect_text(const std::string &got, const std::string &want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s:\n  got  '%s'\n  want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
  }
}

int main() {
  using namespace apollide::display_seat;

  // 11-04 19:43: Artemide quit Desktop while the browser view still showed its display, then launched again
  expect(take_own_seat_back(true, true, 1, false), true, "own ending seat still shown: take it back");
  expect(take_own_seat_back(true, true, 0, false), false, "no capture left: the normal release finishes");
  expect(take_own_seat_back(true, false, 1, false), false, "seat not ending: the normal path already reuses it");
  expect(take_own_seat_back(false, true, 1, false), false, "no game seat: nothing to take back");
  expect(take_own_seat_back(true, true, 1, true), false, "protected output or shutdown: keep refusing");

  expect_text(describe_holders({{"Surface", true}, {"Mac", false}}),
              "Surface (app ended, still shown by a browser view), Mac", "holders listed with their state");
  expect_text(describe_holders({{"", false}}), "(unnamed client)", "unnamed holder stays readable");

  expect_text(refusal_message(refusal_e::capacity, "Surface, Mac, iPad, TV", 4, 4),
              "Virtual display client limit reached (4/4: Surface, Mac, iPad, TV)", "capacity names the holders");
  expect(refusal_message(refusal_e::capacity, "", 4, 4).rfind("Virtual display client limit reached", 0) == 0, true,
         "capacity keeps the old prefix");
  const auto held = refusal_message(refusal_e::held_by_capture, "Surface (app ended, still shown by a browser view)", 1, 4);
  expect(held.find("browser (WebRTC) view") != std::string::npos, true, "held seat names the browser view");
  expect(held.find("client limit") == std::string::npos, true, "held seat is not called a client limit");
  expect(refusal_message(refusal_e::busy, "", 0, 4).find("paused") != std::string::npos, true, "busy says why");

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("display_seat_math: ok\n");
  return 0;
}
