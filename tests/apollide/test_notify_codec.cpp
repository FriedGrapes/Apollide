// Apollide: standalone test for notify_codec.h (g++ -std=c++20 -I. tests/apollide/test_notify_codec.cpp)
#include "src/apollide/notify_codec.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

static std::string as_string(const std::vector<std::uint8_t> &v) {
  return std::string(v.begin(), v.end());
}

int main() {
  using namespace apollide::notify;

  // UTF-8 truncation never splits a character ("가" is 3 bytes)
  expect(utf8_truncate("가나다", 7) == "가나", "truncate at character boundary");
  expect(utf8_truncate("abc", 10) == "abc", "short string untouched");
  expect(utf8_truncate("ab가", 3) == "ab", "drop partial trailing character");

  // Notification packet: header + payload exactly as section 3-1 of the design
  notification_t n {"App!Id", "Claude", "Done", "Line1\nLine2", std::string("\x89PNG", 4)};
  const auto pkt = notification_packet(0x01020304, 7, 1500, n);
  std::vector<std::uint8_t> want {0x20, 0x3A, 0, 0, 1, 4, 3, 2, 1, 7, 0, 0, 0, 0xDC, 0x05, 0, 0};
  auto add16 = [&](std::string_view s) {
    want.push_back(static_cast<std::uint8_t>(s.size() & 0xFF));
    want.push_back(static_cast<std::uint8_t>(s.size() >> 8));
    want.insert(want.end(), s.begin(), s.end());
  };
  add16("App!Id");
  add16("Claude");
  add16("Done");
  add16("Line1\nLine2");
  add16(std::string("\x89PNG", 4));
  const auto payload_len = want.size() - 4;
  want[2] = static_cast<std::uint8_t>(payload_len & 0xFF);
  want[3] = static_cast<std::uint8_t>(payload_len >> 8);
  expect(pkt == want, "Notification bytes");

  // Long bodies are cut so the packet stays well under the control stream limit
  notification_t big {"a", "b", std::string(9000, 'x'), std::string(9000, 'y'), std::string(70000, 'z')};
  const auto big_pkt = notification_packet(1, 1, 0, big);
  expect(big_pkt.size() < 16000, "big notification capped");

  expect(notify_state_packet(true, helper_state_e::ok, 9) == std::vector<std::uint8_t> {0x22, 0x3A, 7, 0, 1, 1, 1, 9, 0, 0, 0}, "NotifyState bytes");

  // NotifySubscribe parse
  const auto sub = parse_subscribe(std::string("\x01\x01\x04\x03\x02\x01\x39\x00\x00\x00", 10));
  expect(sub && sub->enabled && sub->boot == 0x01020304 && sub->last_seq == 57, "NotifySubscribe parse");
  expect(!parse_subscribe(std::string("\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00", 10)), "NotifySubscribe unknown version");
  expect(!parse_subscribe(std::string("\x01\x01\x00", 3)), "NotifySubscribe too short");

  // Helper pipe messages round trip
  const auto ready = parse_helper(as_string(helper_ready(helper_state_e::denied)));
  expect(ready && ready->type == helper_type_e::ready && ready->access == helper_state_e::denied, "helper ready");
  const auto beat = parse_helper(as_string(helper_heartbeat(123456)));
  expect(beat && beat->type == helper_type_e::heartbeat && beat->idle_ms == 123456, "helper heartbeat");
  const auto note = parse_helper(as_string(helper_notification(42, n)));
  expect(note && note->type == helper_type_e::notification && note->idle_ms == 42 && note->n.app == "Claude" && note->n.body == "Line1\nLine2" && note->n.icon.size() == 4, "helper notification");
  expect(!parse_helper(std::string("\x03\x00", 2)), "helper truncated notification");
  expect(!parse_helper(std::string("\x09", 1)), "helper unknown type");

  // Own notifications are not forwarded
  expect(is_own_app("Apollide") && is_own_app("Vibepollo") && is_own_app("Sunshine") && !is_own_app("Claude"), "own apps filtered");

  // 'Someone is using H1 directly': no stream, recent input that came after the last stream ended
  constexpr std::int64_t min = 60'000;
  expect(!local_use(true, 1000, -1), "streaming is never local use");
  expect(local_use(false, 1000, -1), "recent input, never streamed");
  expect(!local_use(false, 6 * min, -1), "idle for 6 minutes");
  expect(!local_use(false, 2 * min, 2 * min), "last input was the stream itself");
  expect(!local_use(false, 2 * min - 2000, 2 * min), "input right as the stream ended");
  expect(local_use(false, 1 * min, 2 * min), "input a minute after the stream ended");

  // Catch-up selection (section 3-2)
  std::vector<stored_t> items;
  const std::int64_t now = 100 * min;
  for (std::uint32_t s = 1; s <= 30; ++s) {
    items.push_back({{"id", "app", "t" + std::to_string(s), "", ""}, s, now - (30 - s) * 30'000});  // one every 30 s
  }
  const auto after = select_since(items, 5, 5, 25, now);
  expect(after.size() == 5 && after.front() == 25 && after.back() == 29, "same boot: newer than after");
  const auto other = select_since(items, 5, 4, 25, now);
  // last 10 minutes = 20 items (s 11..30), capped at 20
  expect(other.size() == 20 && items[other.front()].seq == 11, "other boot: last 10 minutes");
  expect(select_since(items, 5, 5, 30, now).empty(), "nothing new");
  std::vector<stored_t> many;
  for (std::uint32_t s = 1; s <= 50; ++s) {
    many.push_back({{"id", "app", "", "", ""}, s, now});
  }
  const auto capped = select_since(many, 5, 0, 0, now);
  expect(capped.size() == 20 && many[capped.front()].seq == 31, "at most the newest 20");

  // Base64 for the HTTPS icon field
  expect(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=" && base64("foo") == "Zm9v" && base64("foobar") == "Zm9vYmFy", "base64");

  if (failures == 0) {
    std::printf("notify codec: all passed\n");
  }
  return failures == 0 ? 0 : 1;
}
