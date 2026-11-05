// Apollide: standalone test for webrtc_offer_watch_math.h (g++ -std=c++20 -I. tests/apollide/test_webrtc_offer_watch_math.cpp)
#include "src/apollide/webrtc_offer_watch_math.h"

#include <cstdio>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::webrtc_offer_watch;
  using namespace std::chrono_literals;

  // 11-05 14:20: the browser asked for 960x1600, the virtual display took longer than
  // Kiln's 15 s, Kiln gave up, and the late session (no offer ever) held the only capture
  // for five minutes, so every 1920x1080 request was refused.
  expect(should_close({.found = true, .has_remote_offer = false}), true, "no offer by the deadline: close it");

  // A normal viewer sends its offer right after the create answer
  expect(should_close({.found = true, .has_remote_offer = true}), false, "offer arrived: keep it");

  // Already closed by the viewer, Kiln's orphan sweep or a peer failure: nothing to do
  expect(should_close({.found = false, .has_remote_offer = false}), false, "session already gone: nothing to close");

  // The deadline must outlast a real offer round trip but free the capture well before
  // Kiln's 60 s create wait would retry with another size
  expect(offer_deadline >= 20s && offer_deadline <= 30s, true, "deadline is 20 to 30 seconds");

  if (failures == 0) {
    std::printf("webrtc_offer_watch_math: ok\n");
  }
  return failures == 0 ? 0 : 1;
}
