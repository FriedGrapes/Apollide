// Apollide: standalone test for update_policy.h (g++ -std=c++20 -I. tests/apollide/test_update_policy.cpp)
#include "src/apollide/update_policy.h"

#include <cstdio>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::update_policy;
  using namespace std::chrono_literals;

  const int noon = 12 * 60;
  const int five = 5 * 60;

  // Nobody streaming: as before, ten quiet minutes and it installs (any time of day)
  situation_t idle_host {.pending = true, .watching = false, .quiet_for = 11min, .idle_for = 0s, .local_minutes = noon};
  expect(decide(idle_host) == action_e::install, true, "no stream for 10 minutes: install");
  idle_host.quiet_for = 5min;
  expect(decide(idle_host) == action_e::install, false, "a stream ended 5 minutes ago: wait");

  // 11-03: a Kiln browser stream was not counted, so an install could cut it off
  situation_t watched {.pending = true, .watching = true, .quiet_for = 0s, .idle_for = 2h, .local_minutes = noon};
  expect(decide(watched) == action_e::install, false, "someone is watching at noon: wait, even with no input");

  // Night tidy-up: watching, 4-6 a.m., 30 minutes without input
  watched.local_minutes = five;
  watched.idle_for = 31min;
  expect(decide(watched) == action_e::install, true, "5 a.m., 31 minutes without input: install");
  watched.idle_for = 10min;
  expect(decide(watched) == action_e::install, false, "5 a.m. but someone moved the mouse 10 minutes ago: wait");
  watched.idle_for = 2h;
  watched.local_minutes = 3 * 60 + 59;
  expect(decide(watched) == action_e::install, false, "3:59 is before the window");
  watched.local_minutes = 4 * 60;
  expect(decide(watched) == action_e::install, true, "4:00 opens the window");
  watched.local_minutes = 6 * 60;
  expect(decide(watched) == action_e::install, false, "6:00 closes it: next day");

  situation_t nothing {.pending = false, .watching = false, .quiet_for = 5h, .idle_for = 5h, .local_minutes = five};
  expect(decide(nothing) == action_e::install, false, "nothing newer: never install");

  // The on-screen question
  expect(should_ask(true, true, false, false), true, "watching with an update waiting: ask");
  expect(should_ask(true, true, true, false), false, "'later' was chosen for this release: do not ask again");
  expect(should_ask(true, false, false, false), false, "nobody watching: it installs quietly, no question");
  expect(should_ask(false, true, false, false), false, "nothing newer: no question");
  expect(should_ask(true, true, false, true), false, "already installing: no question");

  // The Kiln route: this PC only; checking is harmless, installing needs the API token
  expect(route_allowed(false, true, route_e::status), false, "another PC: never");
  expect(route_allowed(true, false, route_e::check), true, "apollide.py release on this PC may ask for a check");
  expect(route_allowed(true, false, route_e::install), false, "installing needs the token");
  expect(route_allowed(true, false, route_e::later), false, "'later' needs the token");
  expect(route_allowed(true, false, route_e::status), false, "status needs the token");
  expect(route_allowed(true, true, route_e::install), true, "Kiln with its token may install");
  expect(parse_route("install") == route_e::install, true, "install");
  expect(parse_route("later") == route_e::later, true, "later");
  expect(parse_route("check") == route_e::check, true, "check");
  expect(parse_route("") == route_e::status, true, "empty: status");
  expect(parse_route("format-disk") == route_e::unknown, true, "anything else is refused");

  // A check asked from this PC at most once a minute (GitHub allows 60 unauthenticated calls an hour)
  expect(check_due(59s), false, "59 seconds after the last: skip");
  expect(check_due(60s), true, "a minute later: check");

  if (failures == 0) {
    std::printf("update_policy: all passed\n");
  }
  return failures == 0 ? 0 : 1;
}
