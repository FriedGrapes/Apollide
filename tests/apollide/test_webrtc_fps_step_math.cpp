// Apollide: standalone test for webrtc_fps_step_math.h (g++ -std=c++20 -I. tests/apollide/test_webrtc_fps_step_math.cpp)
#include "src/apollide/webrtc_fps_step_math.h"

#include <cstdio>

static int failures = 0;

static void expect_int(long long got, long long want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %lld want %lld\n", what, got, want);
    ++failures;
  }
}

static void expect(bool got, bool want, const char *what) {
  expect_int(got, want, what);
}

int main() {
  using namespace apollide::webrtc_fps_step;
  using namespace std::chrono_literals;
  const auto t0 = clock::time_point {} + 100s;

  // Floor: 1500, or the session's own lower bitrate
  expect_int(floor_for(20000), 1500, "floor for a 20 Mbps session");
  expect_int(floor_for(1000), 1000, "floor never above the configured bitrate");
  expect_int(floor_for(0), 1, "floor stays positive");

  // 11-18 17:05:08: one 0.3 s dip to 232 kbps that came straight back must change nothing
  {
    state_t s;
    expect(step(s, t0, 2083), false, "low estimate starts the clock");
    expect(step(s, t0 + 100ms, 232), false, "still inside the 2 s wait");
    expect(step(s, t0 + 300ms, 9000), false, "recovered: clock reset");
    expect(step(s, t0 + 2500ms, 9000), false, "no change while full and fast");
    expect(s.halved, false, "a brief dip keeps 60 fps");
  }

  // A link that stays slow: 30 fps after 2 s, back to 60 only after 5 s of >= 8000
  {
    state_t s;
    step(s, t0, 3000);
    expect(step(s, t0 + 1999ms, 3000), false, "1.999 s low: not yet");
    expect(step(s, t0 + 2000ms, 3000), true, "2 s low: halve");
    expect(s.halved, true, "now 30 fps");
    expect(step(s, t0 + 3s, 6000), false, "6000 is between the two lines: stay halved");
    expect(step(s, t0 + 4s, 8000), false, "8000 starts the restore clock");
    expect(step(s, t0 + 8s, 8500), false, "4 s fast: not yet");
    expect(step(s, t0 + 6s + 3s, 9000), true, "5 s fast: back to 60");
    expect(s.halved, false, "full rate again");
  }

  // A restore that wobbles below 8000 starts over
  {
    state_t s;
    s.halved = true;
    step(s, t0, 9000);
    step(s, t0 + 4s, 7000);
    expect(step(s, t0 + 6s, 9000), false, "wobble reset the restore clock");
    expect(step(s, t0 + 10s, 9000), false, "4 s since the restart");
    expect(step(s, t0 + 11s, 9000), true, "5 s since the restart");
  }

  // Encoder gets twice the target while halved, capped by the configured bitrate
  expect_int(encoder_kbps(false, 3000, 20000), 3000, "full rate: as estimated");
  expect_int(encoder_kbps(true, 3000, 20000), 6000, "halved: twice the target");
  expect_int(encoder_kbps(true, 15000, 20000), 20000, "halved: capped at configured");
  expect_int(encoder_kbps(true, 25000, 20000), 25000, "never below the target itself");

  // Frame gate at 60 fps (16.67 ms): keep frames >= ~29 ms apart while halved
  {
    const auto interval = std::chrono::duration_cast<clock::duration>(16666667ns);
    expect(skip_frame(false, 1ms, interval), false, "full rate never skips");
    expect(skip_frame(true, 16ms, interval), true, "next 60 fps frame is skipped");
    expect(skip_frame(true, 33ms, interval), false, "the one after is sent");
    expect(skip_frame(true, 30ms, interval), false, "a little early still sent (jitter slack)");
    expect(skip_frame(true, 24ms, interval), true, "too early: skipped");
  }

  if (failures == 0) {
    std::printf("webrtc_fps_step_math: all ok\n");
  }
  return failures == 0 ? 0 : 1;
}
