/**
 * @file src/apollide/update_link_codec.h
 * @brief Apollide: update-link control messages (header only, so they are testable on their own).
 *
 * Wire format (little endian, strings are u8 length + UTF-8), shared with Artemide —
 * see NAS Apollide/_docs/계획/2026-10-24-업데이트-연동.md, section 3:
 *   0x3A10 host -> client  HostUpdate      u8 ver(1), u8 state, str current, str latest
 *   0x3A11 client -> host  ClientHello     u8 ver(1), u8 state, str current, str latest, str name
 *   0x3A12 client -> host  UpdateHostNow   u8 ver(1)
 *   0x3A13 host -> client  UpdateClientNow u8 ver(1)
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apollide::link {
  constexpr std::uint16_t kHostUpdate = 0x3A10;
  constexpr std::uint16_t kClientHello = 0x3A11;
  constexpr std::uint16_t kUpdateHostNow = 0x3A12;
  constexpr std::uint16_t kUpdateClientNow = 0x3A13;

  /// ClientHello state
  enum class client_state_e : std::uint8_t {
    latest = 0,
    available = 1,
    updating = 2
  };

  struct client_hello_t {
    client_state_e state;
    std::string current;
    std::string latest;
    std::string name;
  };

  namespace detail {
    inline void put_str(std::vector<std::uint8_t> &out, std::string_view s) {
      const auto n = s.size() > 255 ? 255 : s.size();
      out.push_back(static_cast<std::uint8_t>(n));
      out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }

    inline bool get_str(std::string_view &in, std::string &out) {
      if (in.empty()) {
        return false;
      }
      const auto n = static_cast<std::uint8_t>(in.front());
      in.remove_prefix(1);
      if (in.size() < n) {
        return false;
      }
      out.assign(in.substr(0, n));
      in.remove_prefix(n);
      return true;
    }
  }  // namespace detail

  /// Complete control packet: u16 type, u16 payload length, payload
  inline std::vector<std::uint8_t> packet(std::uint16_t type, const std::vector<std::uint8_t> &payload) {
    std::vector<std::uint8_t> out {
      static_cast<std::uint8_t>(type & 0xFF), static_cast<std::uint8_t>(type >> 8),
      static_cast<std::uint8_t>(payload.size() & 0xFF), static_cast<std::uint8_t>(payload.size() >> 8)
    };
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }

  inline std::vector<std::uint8_t> host_update(std::uint8_t state, std::string_view current, std::string_view latest) {
    std::vector<std::uint8_t> payload {1, state};
    detail::put_str(payload, current);
    detail::put_str(payload, latest);
    return packet(kHostUpdate, payload);
  }

  inline std::vector<std::uint8_t> update_client_now() {
    return packet(kUpdateClientNow, {1});
  }

  /// Payload after the 4-byte control header
  inline std::optional<client_hello_t> parse_client_hello(std::string_view payload) {
    if (payload.size() < 2 || static_cast<std::uint8_t>(payload[0]) != 1) {
      return std::nullopt;
    }
    client_hello_t hello {};
    const auto state = static_cast<std::uint8_t>(payload[1]);
    if (state > 2) {
      return std::nullopt;
    }
    hello.state = static_cast<client_state_e>(state);
    payload.remove_prefix(2);
    if (!detail::get_str(payload, hello.current) || !detail::get_str(payload, hello.latest)) {
      return std::nullopt;
    }
    detail::get_str(payload, hello.name);  // optional
    return hello;
  }

  /// "0.2.10" > "0.2.9"; anything unparsable compares as 0.0.0
  inline int compare_versions(std::string_view a, std::string_view b) {
    auto parts = [](std::string_view v) {
      int p[3] = {0, 0, 0};
      for (int i = 0; i < 3 && !v.empty(); ++i) {
        int n = 0;
        while (!v.empty() && v.front() >= '0' && v.front() <= '9') {
          n = n * 10 + (v.front() - '0');
          v.remove_prefix(1);
        }
        p[i] = n;
        if (!v.empty() && v.front() == '.') {
          v.remove_prefix(1);
        } else {
          break;
        }
      }
      return std::vector<int> {p[0], p[1], p[2]};
    };
    const auto x = parts(a), y = parts(b);
    for (int i = 0; i < 3; ++i) {
      if (x[i] != y[i]) {
        return x[i] < y[i] ? -1 : 1;
      }
    }
    return 0;
  }
}  // namespace apollide::link
