// Apollide: standalone test for kiln_viewers_math.h (g++ -std=c++20 -I. tests/apollide/test_kiln_viewers_math.cpp)
#include "src/apollide/kiln_viewers_math.h"

#include <cstdio>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::kiln_viewers;

  const std::vector<paired_t> paired {{"MAC-UUID", "Mac"}, {"SURF-UUID", "Surface"}};

  // The Mac streams the game picture; the Surface only sends input; a stopped stream is leaving
  const std::vector<stream_t> streams {
    {.client_uuid = "MAC-UUID", .device_name = "MacBook-Pro", .width = 1920, .height = 1080, .fps = 60, .role = "game", .state = "running"},
    {.client_uuid = "SURF-UUID", .device_name = "SURFACE", .width = 0, .height = 0, .fps = 0, .role = "game", .input_only = true, .state = "running"},
    {.client_uuid = "OLD-UUID", .device_name = "Old", .width = 1280, .height = 720, .fps = 30, .role = "game", .state = "stopped"},
  };
  const app_t app {.running = true, .client_uuid = "MAC-UUID", .name = "Desktop"};

  auto v = list_viewers(streams, paired, app);
  expect(v.size() == 1, true, "only the live picture stream counts (no input-only, no stopped)");
  expect(!v.empty() && v[0].name == "Mac", true, "the paired name wins over the device name");
  expect(!v.empty() && v[0].width == 1920 && v[0].fps == 60 && !v[0].paused, true, "size and fps of the live stream");
  expect(!v.empty() && v[0].kind == "moonlight", true, "RTSP clients are kind moonlight");

  // The Mac quit its stream but its app is still running (paused): it holds the picture
  auto paused = list_viewers({}, paired, app);
  expect(paused.size() == 1 && paused[0].paused && paused[0].name == "Mac", true, "a paused app shows its owner as paused");
  expect(paused.size() == 1 && paused[0].app == "Desktop", true, "the paused entry names the app");

  // A Web UI launch has no client: paused, but nobody to name
  auto anonymous = list_viewers({}, paired, {.running = true, .client_uuid = "", .name = "Desktop"});
  expect(anonymous.empty(), true, "an app started without a client is not a viewer");

  // An unknown device keeps its own name
  auto stranger = list_viewers({{.client_uuid = "X", .device_name = "Phone", .width = 800, .height = 600, .fps = 60, .role = "game", .state = "running"}}, paired, {});
  expect(stranger.size() == 1 && stranger[0].name == "Phone", true, "unpaired name falls back to the device name");

  // The same client with a game and a monitor stream is listed per stream
  auto both = list_viewers({
    {.client_uuid = "MAC-UUID", .device_name = "m", .width = 1920, .height = 1080, .fps = 60, .role = "game", .state = "running"},
    {.client_uuid = "MAC-UUID", .device_name = "m", .width = 1280, .height = 800, .fps = 60, .role = "monitor", .state = "starting"},
  }, paired, app);
  expect(both.size() == 2 && both[1].role == "monitor", true, "a second-monitor stream is its own entry");

  // Disconnect targets
  auto all = pick_targets({.uuids = {}, .all = true}, v);
  expect(!all.bad && all.uuids == std::vector<std::string> {"MAC-UUID"}, true, "all: every live viewer once");
  auto all_paused = pick_targets({.uuids = {}, .all = true}, paused);
  expect(!all_paused.bad && all_paused.uuids.empty(), true, "all: a paused owner has no stream to end");

  auto some = pick_targets({.uuids = {"MAC-UUID", "MAC-UUID", "NOPE", ""}}, both);
  expect(!some.bad && some.uuids == std::vector<std::string> {"MAC-UUID"}, true, "uuids: live ones once, duplicates dropped");
  expect(some.unknown == std::vector<std::string> {"NOPE"}, true, "uuids: ones not streaming are reported back");

  auto none = pick_targets({}, v);
  expect(none.bad, true, "neither uuids nor all is a bad request");

  if (failures == 0) {
    std::printf("kiln_viewers_math: all passed\n");
  }
  return failures == 0 ? 0 : 1;
}
