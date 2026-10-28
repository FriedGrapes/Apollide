// Apollide: standalone test for update_link_codec.h (g++ -std=c++20 -I. tests/apollide/test_update_link_codec.cpp)
#include "src/apollide/update_link_codec.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

int main() {
  using namespace apollide::link;

  // HostUpdate: header + payload exactly as section 3 of the design
  const auto pkt = host_update(3, "0.2.2", "0.2.3");
  const std::vector<std::uint8_t> want {0x10, 0x3A, 14, 0, 1, 3, 5, '0', '.', '2', '.', '2', 5, '0', '.', '2', '.', '3'};
  expect(pkt == want, "HostUpdate bytes");
  expect(update_client_now() == std::vector<std::uint8_t> {0x13, 0x3A, 1, 0, 1}, "UpdateClientNow bytes");

  // ClientHello round trip
  std::string hello {"\x01\x02"};
  hello += '\x05'; hello += "0.2.7";
  hello += '\x05'; hello += "0.2.8";
  hello += '\x07'; hello += "Surface";
  const auto h = parse_client_hello(hello);
  expect(h && h->state == client_state_e::updating && h->current == "0.2.7" && h->latest == "0.2.8" && h->name == "Surface", "ClientHello parse");

  std::string no_name("\x01\x00", 2);
  no_name += '\x05'; no_name += "0.2.7";
  no_name += '\x00';
  const auto n = parse_client_hello(no_name);
  expect(n && n->latest.empty() && n->name.empty(), "ClientHello without latest and name");

  expect(!parse_client_hello(std::string("\x02\x00\x00\x00", 4)), "unknown version");
  expect(!parse_client_hello(std::string("\x01\x07\x00\x00", 4)), "unknown state");
  expect(!parse_client_hello(std::string("\x01\x00\x09" "0.2", 6)), "truncated string");

  expect(compare_versions("0.2.10", "0.2.9") > 0, "0.2.10 > 0.2.9");
  expect(compare_versions("0.3.0", "0.3.0") == 0, "equal");
  expect(compare_versions("0.2.2", "0.3.0") < 0, "0.2.2 < 0.3.0");
  expect(compare_versions("", "0.0.1") < 0, "empty is lowest");

  std::printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
