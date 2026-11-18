// Apollide: standalone test for webrtc_join_math.h (g++ -std=c++20 -I. tests/apollide/test_webrtc_join_math.cpp)
#include "src/apollide/webrtc_join_math.h"

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
  using namespace apollide::webrtc_join;

  // 11-04 19:44: the capture resumed Desktop, Artemide quit it, Kiln asks again with app_id 0
  expect(adopt_active_app_identity(0, true), true, "no app named, capture live: join its app");
  expect(adopt_active_app_identity(-1, true), true, "negative id means no app too");
  expect(adopt_active_app_identity(0, false), false, "no capture: nothing to adopt, start fresh");
  expect(adopt_active_app_identity(5, true), false, "an app named on purpose is compared as asked");

  expect(resume_has_nothing_to_join(-1, false), true, "app ended and no capture: nothing to resume");
  expect(resume_has_nothing_to_join(-1, true), false, "app ended but browser capture live: join it");
  expect(resume_has_nothing_to_join(0, true), false, "desktop capture live: join it");
  expect(resume_has_nothing_to_join(3, false), false, "an app runs: resume it");

  expect_text(describe_differences({{"fps", "60", "60"}, {"bitrate_kbps", "20000", "20000"}}), "", "same settings: nothing listed");
  expect_text(
    describe_differences({{"app", "uuid D", "desktop"}, {"fps", "60", "60"}, {"bitrate_kbps", "20000", "25000"}}),
    "app now uuid D, asked desktop; bitrate_kbps now 20000, asked 25000",
    "only differing fields, current then requested"
  );
  expect_text(codec_name(0), "h264", "codec 0");
  expect_text(codec_name(1), "hevc", "codec 1");
  expect_text(codec_name(2), "av1", "codec 2");
  expect_text(codec_name(7), "format 7", "unknown codec stays readable");

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("webrtc_join_math: ok\n");
  return 0;
}
