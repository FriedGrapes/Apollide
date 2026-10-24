// Apollide: standalone test for scroll_route_math.h (g++ -std=c++20 -I. tests/apollide/test_scroll_route_math.cpp)
#include "src/apollide/scroll_route_math.h"

#include <cstdio>

static int failures = 0;

static void expect(long got, long want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %ld want %ld\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::scroll;

  // 1920-pixel-tall video: one notch = 0.06 * 1920 = 115.2 pixels of finger movement
  const float notch = kNotchFraction * 1920.0f;
  float rest = 0.0f;
  expect(wheel_units(115.2f, notch, rest), 120, "one notch of finger movement is one wheel notch");
  expect(wheel_units(-115.2f * 2, notch, rest), -240, "two notches the other way");

  // Small steps are not lost: they add up across events
  rest = 0.0f;
  long total = 0;
  for (int i = 0; i < 100; ++i) {
    total += wheel_units(1.152f, notch, rest);  // a hundredth of a notch each
  }
  expect(total, 119, "hundred small steps add up (one unit still pending)");
  expect(wheel_units(0.02f, notch, rest), 1, "the pending unit comes out with the next step");
  expect(wheel_units(0.0f, 0.0f, rest), 0, "no video height, no wheel");

  expect(is_pan_pointer(0x41D00001), 1, "finger A");
  expect(is_pan_pointer(0x41D00002), 1, "finger B");
  expect(is_pan_pointer(1), 0, "a real touch finger");

  expect(has_classic_scrollbars(0x50200000), 1, "WS_CHILD|WS_VISIBLE|WS_VSCROLL");
  expect(has_classic_scrollbars(0x00100000), 1, "WS_HSCROLL");
  expect(has_classic_scrollbars(0x56000000), 0, "child without scroll bars (e.g. Chrome)");

  expect(scrollbar_visible(0x00000000), 1, "scroll bar on screen (firewall list)");
  expect(scrollbar_visible(0x00010000), 0, "off screen (Chrome, Claude, Whale)");
  expect(scrollbar_visible(0x00008000), 0, "hidden (Explorer file list)");

  expect(menu_open(0x14), 1, "popup menu open (measured on H1: GUI_INMENUMODE|GUI_POPUPMENUMODE)");
  expect(menu_open(0x04), 1, "menu bar menu open");
  expect(menu_open(0x00), 0, "no menu");
  expect(menu_open(0x02), 0, "only a caret blinking (GUI_CARETBLINKING)");

  std::printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
