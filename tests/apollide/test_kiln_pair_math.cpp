// Apollide: standalone test for kiln_pair_math.h (g++ -std=c++20 -I. tests/apollide/test_kiln_pair_math.cpp)
#include "src/apollide/kiln_pair_math.h"

#include <cstdio>

static int failures = 0;

static void expect(bool got, bool want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: got %d want %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace apollide::kiln_pair;
  using namespace std::chrono_literals;
  const auto t0 = pair_clock::time_point {} + 1h;
  std::string token;

  // Nothing asked yet
  book_t book;
  expect(book.pending_code(t0).has_value(), false, "no request: the page has nothing to approve");
  expect(book.poll("x", t0, token) == state_e::none, true, "unknown id: none");

  // Kiln asks, the page shows the code, the person approves, Kiln picks the key up once
  book.start("id1", "4821", t0);
  expect(book.pending_code(t0 + 10s) == std::optional<std::string> {"4821"}, true, "page shows the pending code");
  expect(book.poll("id1", t0 + 1s, token) == state_e::pending, true, "Kiln waits while nobody has answered");
  expect(token.empty(), true, "no key while pending");
  expect(book.approve("1111", t0 + 20s, "KEY"), false, "another code is not this request");
  expect(book.approve("4821", t0 + 20s, "KEY"), true, "the right code approves");
  expect(book.pending_code(t0 + 21s).has_value(), false, "approved: nothing left to approve");
  expect(book.poll("other", t0 + 22s, token) == state_e::none, true, "another id never gets the key");
  expect(token.empty(), true, "another id: still no key");
  expect(book.poll("id1", t0 + 22s, token) == state_e::approved && token == "KEY", true, "Kiln gets the key");
  token.clear();
  expect(book.poll("id1", t0 + 23s, token) == state_e::taken && token.empty(), true, "the key is handed over only once");

  // Denied
  book.start("id2", "1234", t0);
  expect(book.deny("1234", t0 + 5s), true, "the person can say no");
  expect(book.poll("id2", t0 + 6s, token) == state_e::denied && token.empty(), true, "Kiln hears no and gets no key");
  expect(book.approve("1234", t0 + 7s, "KEY"), false, "a refused request cannot be approved afterwards");

  // Expiry: requests wait 5 minutes, an approved key waits 2 minutes for Kiln
  book.start("id3", "5555", t0);
  expect(book.pending_code(t0 + 4min).has_value(), true, "still waiting at 4 minutes");
  expect(book.approve("5555", t0 + 6min, "KEY"), false, "too late to approve after 5 minutes");
  expect(book.poll("id3", t0 + 6min, token) == state_e::expired, true, "Kiln hears the request expired");
  book.start("id4", "6666", t0);
  expect(book.approve("6666", t0 + 1min, "KEY"), true, "approved in time");
  expect(book.poll("id4", t0 + 4min, token) == state_e::expired && token.empty(), true, "a key Kiln never picked up is dropped");

  // A new request replaces the old one
  book.start("id5", "7777", t0);
  book.start("id6", "8888", t0 + 1s);
  expect(book.approve("7777", t0 + 2s, "KEY"), false, "the replaced request cannot be approved");
  expect(book.poll("id5", t0 + 2s, token) == state_e::none, true, "the replaced request is gone");
  expect(book.pending_code(t0 + 2s) == std::optional<std::string> {"8888"}, true, "the newest request is shown");

  // Scopes
  const auto scopes = kiln_scopes();
  bool monitor = false, offer = false, cert = false;
  for (const auto &s : scopes) {
    if (s.path == "/api/kiln/monitor" && s.methods.size() == 3) monitor = true;
    if (s.path == "/api/webrtc/sessions/([A-Fa-f0-9-]+)/offer" && s.methods.size() == 1 && s.methods[0] == "POST") offer = true;
    if (s.path == "/api/webrtc/cert") cert = true;
    if (s.path.find("/api/kiln/pair") != std::string::npos) expect(true, false, "the Kiln key never approves its own links");
    if (s.path.find("/api/token") != std::string::npos) expect(true, false, "the Kiln key never makes keys");
  }
  expect(scopes.size() == 9, true, "nine scopes, as the manual key");
  expect(monitor && offer && cert, true, "monitor, offer and cert scopes are in");

  if (failures == 0) {
    std::printf("kiln_pair_math: all passed\n");
  }
  return failures == 0 ? 0 : 1;
}
