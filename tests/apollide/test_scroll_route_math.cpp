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

  // Virtual touchpad: movement from where the fingers went down, at kPadScale of the pad
  expect(static_cast<long>(pad_target(1365.0f, 0.5f, 0.5f, 2730)), 1365, "no movement stays at the anchor");
  expect(static_cast<long>(pad_target(1365.0f, 0.6f, 0.5f, 2730)), 1474, "10% of the video is 4% of the pad");
  expect(static_cast<long>(pad_target(1365.0f, 0.0f, 1.0f, 2730)), 273, "a full sweep up stays on the pad");
  expect(static_cast<long>(pad_target(2700.0f, 1.0f, 0.0f, 2730)), 2730, "never past the pad edge");
  // Interpolation closes most of the gap within a few ticks and never overshoots
  float pos = 0.0f;
  for (int i = 0; i < 4; ++i) pos = pad_step(pos, 100.0f);
  expect(pos > 80.0f && pos < 100.0f, 1, "four 4 ms ticks cover over 80% of a jump");
  expect(static_cast<long>(pad_step(100.0f, 100.0f)), 100, "at the target it stays put");

  // Wheel app list
  const auto apps = parse_app_list("# apps that scroll as wheel\r\n  Claude.exe  \n\n notepad.exe # editor\n");
  expect(static_cast<long>(apps.size()), 2, "two apps, comments and blank lines skipped");
  expect(apps.size() == 2 && apps[0] == "claude.exe" && apps[1] == "notepad.exe", 1, "names trimmed and lower-cased");
  expect(static_cast<long>(parse_app_list("").size()), 0, "empty file turns the rule off");
  expect(kDefaultWheelApps.size() == 1 && kDefaultWheelApps[0] == "claude.exe", 1, "default is the Claude app");

  std::printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
