// Apollide: standalone test for resume_mode_math.h (g++ -std=c++20 -I. tests/apollide/test_resume_mode_math.cpp)
#include "src/apollide/resume_mode_math.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::resume_mode;

  const situation_t idle {.nobody_watching = true, .input_only = false, .virtual_output = true};
  const apollide::resume_mode::mode_t surface {2880, 1920, 120000};
  const apollide::resume_mode::mode_t mac {3024, 1964, 120000};

  expect(should_switch(idle, surface, surface), false, "same client, same mode: leave the layout alone");
  expect(should_switch(idle, surface, mac), true, "other client with another size: switch");
  expect(should_switch(idle, mac, surface), true, "and back again");
  expect(should_switch(idle, {2880, 1920, 60000}, surface), true, "only the rate differs: switch");
  expect(should_switch(idle, {2880, 1920, 60000}, {2880, 1920, 59940}), false, "59.94 and 60 are the same mode");

  situation_t watched = idle;
  watched.nobody_watching = false;
  expect(should_switch(watched, surface, mac), false, "someone is watching: never change their picture");

  situation_t input = idle;
  input.input_only = true;
  expect(should_switch(input, surface, mac), false, "input only: no display changes");

  situation_t physical = idle;
  physical.virtual_output = false;
  expect(should_switch(physical, surface, mac), false, "physical monitor: never touched");

  expect(should_switch(idle, surface, {0, 0, 0}), false, "no requested mode: leave it");

  const auto c = hz_candidates(59940);
  expect(c.size() == 2 && c[0] == 60 && c[1] == 59, true, "59.94 tries 60 then 59");
  const auto d = hz_candidates(120000);
  expect(d.size() == 1 && d[0] == 120, true, "120 tries 120");

  // A resuming client takes the picture over from whoever is watching it
  const std::vector<viewer_t> mac_watching {{"mac", true}};
  expect(should_take_over(false, "surface", mac_watching), true, "another device is watching: it is disconnected");
  expect(should_take_over(false, "surface", {}), false, "nobody watching: nothing to disconnect");
  expect(should_take_over(false, "mac", mac_watching), false, "the same device reconnecting is not a take-over");
  expect(should_take_over(true, "surface", mac_watching), false, "Remote Input never takes the picture over");
  expect(should_take_over(false, "", mac_watching), false, "no caller identity: leave everyone alone");
  const std::vector<viewer_t> mac_monitor {{"mac", false}};
  expect(should_take_over(false, "surface", mac_monitor), false, "a Remote Monitor/Input stream is not the picture");
  const std::vector<viewer_t> both {{"surface", true}, {"mac", true}};
  expect(should_take_over(false, "surface", both), true, "any other viewer is enough");

  // A mode the display does not list means the display itself has to be made again in the new mode
  expect(needs_recreate(-2), true, "DISP_CHANGE_BADMODE: the mode is not in the display's list, make it again");
  expect(needs_recreate(-1), false, "DISP_CHANGE_FAILED: something else went wrong, leave the display alone");
  expect(needs_recreate(1), false, "DISP_CHANGE_RESTART: not a missing mode");
  expect(needs_recreate(0), false, "success: nothing to make again");

  // Making the display again releases the old one first, so a failed make must never leave it gone
  const apollide::resume_mode::mode_t square {1440, 1440, 120000};
  const apollide::resume_mode::mode_t surface_small {1920, 1280, 120000};
  const auto tries = remake_attempts(surface_small, square);
  expect(tries.size() == 3, true, "new mode twice, then the old mode");
  expect(tries.size() == 3 && tries[0].mode.width == 1920 && !tries[0].fallback, true, "first: the resuming client's mode");
  expect(tries.size() == 3 && tries[1].mode.height == 1280 && !tries[1].fallback, true, "second: the same mode once more, now that the old display is released");
  expect(tries.size() == 3 && tries[2].mode.width == 1440 && tries[2].mode.height == 1440 && tries[2].fallback, true, "last: bring the display back as it was");
  expect(tries.size() == 3 && tries[2].mode.refresh_millihz == 120000, true, "the old mode keeps its rate");
  const auto no_old = remake_attempts(surface_small, {0, 0, 0});
  expect(no_old.size() == 2, true, "no known old mode: only the new mode is tried");

  // 11-02: a browser stream in the night put the dummy HDMI back on; the morning resume joined
  // without laying the displays out again, so the lock screen showed only on the dummy
  const std::string surface_vd = "{B55A691F-65DC-5B55-8355-A13CC94846E1}";
  const std::string dummy = "{DUMMY-HDMI}";
  const std::string webrtc_vd = "{F2FB0AFE-27B0-5992-B774-FD59A0550B90}";
  const std::string kiln_vd = "{KILN-WORKMON}";
  expect(layout_needs_reassert({{dummy}, {webrtc_vd}, {surface_vd}}, {{surface_vd}}), true, "dummy and a stray display are on: lay out again");
  expect(layout_needs_reassert({{surface_vd}}, {{surface_vd}}), false, "only the stream's display: leave it");
  expect(layout_needs_reassert({{"{b55a691f-65dc-5b55-8355-a13cc94846e1}"}}, {{surface_vd}}), false, "device ids differ only in case");
  expect(layout_needs_reassert({{surface_vd}, {kiln_vd}}, {{surface_vd}, {kiln_vd}}), false, "the Kiln work monitor may stay");
  expect(layout_needs_reassert({{kiln_vd}, {surface_vd}}, {{surface_vd}, {kiln_vd}}), false, "order does not matter");
  expect(layout_needs_reassert({{dummy}}, {{surface_vd}}), true, "the stream's display is off: lay out again");
  expect(layout_needs_reassert({{surface_vd}}, {}), false, "nothing wanted: never apply an empty layout");
  expect(layout_needs_reassert({{surface_vd}}, {{""}}), false, "unknown stream display: leave it");

  if (failures == 0) {
    std::printf("resume_mode_math: all passed\n");
  }
  return failures == 0 ? 0 : 1;
}
