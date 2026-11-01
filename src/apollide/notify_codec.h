/**
 * @file src/apollide/notify_codec.h
 * @brief Apollide: notification forwarding messages (header only, so they are testable on their own).
 *
 * Wire format (little endian; str16 = u16 length + UTF-8), shared with Artemide —
 * see NAS Apollide/_docs/계획/2026-10-25-알림-전달.md, section 3:
 *   0x3A20 host -> client  Notification    u8 ver(1), u32 boot, u32 seq, u32 age_ms,
 *                                          str16 appId, str16 app, str16 title, str16 body, u16 iconLen + PNG
 *   0x3A21 client -> host  NotifySubscribe u8 ver(1), u8 enabled, u32 boot, u32 lastSeq
 *   0x3A22 host -> client  NotifyState     u8 ver(1), u8 forwarding, u8 helper, u32 boot
 *
 * The user-session helper (tools/apollide_notify.cpp) talks to the host over a message pipe:
 *   1 Ready        u8 access (helper_state_e)
 *   2 Heartbeat    u32 idle_ms
 *   3 Notification u32 idle_ms, str16 appId, str16 app, str16 title, str16 body, str16 icon
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "update_link_codec.h"

namespace apollide::notify {
  constexpr std::uint16_t kNotification = 0x3A20;
  constexpr std::uint16_t kNotifySubscribe = 0x3A21;
  constexpr std::uint16_t kNotifyState = 0x3A22;

  constexpr std::size_t kMaxText = 4000;  ///< bytes per title/body
  constexpr std::size_t kMaxIcon = 24 * 1024;
  constexpr std::int64_t kKeepMs = 10 * 60 * 1000;  ///< catch-up window
  constexpr std::size_t kMaxCatchUp = 20;
  constexpr std::int64_t kLocalUseMs = 5 * 60 * 1000;
  constexpr std::int64_t kStreamEndGraceMs = 3000;

  enum class helper_state_e : std::uint8_t {
    none = 0,  ///< not running yet
    ok = 1,
    denied = 2  ///< Windows settings block notification access
  };

  enum class helper_type_e : std::uint8_t {
    ready = 1,
    heartbeat = 2,
    notification = 3
  };

  struct notification_t {
    std::string app_id;
    std::string app;
    std::string title;
    std::string body;
    std::string icon;  ///< PNG bytes, may be empty
  };

  struct stored_t {
    notification_t n;
    std::uint32_t seq;
    std::int64_t arrived_ms;
  };

  struct subscribe_t {
    bool enabled;
    std::uint32_t boot;
    std::uint32_t last_seq;
  };

  struct helper_message_t {
    helper_type_e type;
    helper_state_e access = helper_state_e::none;
    std::uint32_t idle_ms = 0;
    notification_t n;
  };

  /// Cut to at most max bytes without splitting a UTF-8 character
  inline std::string utf8_truncate(std::string_view s, std::size_t max) {
    if (s.size() <= max) {
      return std::string(s);
    }
    std::size_t end = max;
    while (end > 0 && (static_cast<std::uint8_t>(s[end]) & 0xC0) == 0x80) {
      --end;
    }
    return std::string(s.substr(0, end));
  }

  namespace detail {
    inline void put_u32(std::vector<std::uint8_t> &out, std::uint32_t v) {
      for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
      }
    }

    inline void put_str16(std::vector<std::uint8_t> &out, std::string_view s) {
      out.push_back(static_cast<std::uint8_t>(s.size() & 0xFF));
      out.push_back(static_cast<std::uint8_t>(s.size() >> 8));
      out.insert(out.end(), s.begin(), s.end());
    }

    inline bool get_u32(std::string_view &in, std::uint32_t &v) {
      if (in.size() < 4) {
        return false;
      }
      v = 0;
      for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[i])) << (8 * i);
      }
      in.remove_prefix(4);
      return true;
    }

    inline bool get_str16(std::string_view &in, std::string &out) {
      if (in.size() < 2) {
        return false;
      }
      const std::size_t n = static_cast<std::uint8_t>(in[0]) | (static_cast<std::size_t>(static_cast<std::uint8_t>(in[1])) << 8);
      in.remove_prefix(2);
      if (in.size() < n) {
        return false;
      }
      out.assign(in.substr(0, n));
      in.remove_prefix(n);
      return true;
    }

    /// Text fields cut to their limits; an oversized icon is dropped rather than cut
    inline notification_t capped(const notification_t &n) {
      return {utf8_truncate(n.app_id, 512), utf8_truncate(n.app, 512), utf8_truncate(n.title, kMaxText), utf8_truncate(n.body, kMaxText), n.icon.size() <= kMaxIcon ? n.icon : std::string()};
    }
  }  // namespace detail

  inline std::vector<std::uint8_t> notification_packet(std::uint32_t boot, std::uint32_t seq, std::uint32_t age_ms, const notification_t &raw) {
    const auto n = detail::capped(raw);
    std::vector<std::uint8_t> payload {1};
    detail::put_u32(payload, boot);
    detail::put_u32(payload, seq);
    detail::put_u32(payload, age_ms);
    detail::put_str16(payload, n.app_id);
    detail::put_str16(payload, n.app);
    detail::put_str16(payload, n.title);
    detail::put_str16(payload, n.body);
    detail::put_str16(payload, n.icon);
    return link::packet(kNotification, payload);
  }

  inline std::vector<std::uint8_t> notify_state_packet(bool forwarding, helper_state_e helper, std::uint32_t boot) {
    std::vector<std::uint8_t> payload {1, static_cast<std::uint8_t>(forwarding ? 1 : 0), static_cast<std::uint8_t>(helper)};
    detail::put_u32(payload, boot);
    return link::packet(kNotifyState, payload);
  }

  /// Payload after the 4-byte control header
  inline std::optional<subscribe_t> parse_subscribe(std::string_view payload) {
    if (payload.size() < 10 || static_cast<std::uint8_t>(payload[0]) != 1) {
      return std::nullopt;
    }
    subscribe_t s {};
    s.enabled = payload[1] != 0;
    payload.remove_prefix(2);
    detail::get_u32(payload, s.boot);
    detail::get_u32(payload, s.last_seq);
    return s;
  }

  // ── helper pipe ──
  inline std::vector<std::uint8_t> helper_ready(helper_state_e access) {
    return {static_cast<std::uint8_t>(helper_type_e::ready), static_cast<std::uint8_t>(access)};
  }

  inline std::vector<std::uint8_t> helper_heartbeat(std::uint32_t idle_ms) {
    std::vector<std::uint8_t> out {static_cast<std::uint8_t>(helper_type_e::heartbeat)};
    detail::put_u32(out, idle_ms);
    return out;
  }

  inline std::vector<std::uint8_t> helper_notification(std::uint32_t idle_ms, const notification_t &raw) {
    const auto n = detail::capped(raw);
    std::vector<std::uint8_t> out {static_cast<std::uint8_t>(helper_type_e::notification)};
    detail::put_u32(out, idle_ms);
    detail::put_str16(out, n.app_id);
    detail::put_str16(out, n.app);
    detail::put_str16(out, n.title);
    detail::put_str16(out, n.body);
    detail::put_str16(out, n.icon);
    return out;
  }

  inline std::optional<helper_message_t> parse_helper(std::string_view in) {
    if (in.empty()) {
      return std::nullopt;
    }
    helper_message_t m {};
    m.type = static_cast<helper_type_e>(static_cast<std::uint8_t>(in[0]));
    in.remove_prefix(1);
    switch (m.type) {
      case helper_type_e::ready:
        if (in.empty() || static_cast<std::uint8_t>(in[0]) > 2) {
          return std::nullopt;
        }
        m.access = static_cast<helper_state_e>(static_cast<std::uint8_t>(in[0]));
        return m;
      case helper_type_e::heartbeat:
        if (!detail::get_u32(in, m.idle_ms)) {
          return std::nullopt;
        }
        return m;
      case helper_type_e::notification:
        if (!detail::get_u32(in, m.idle_ms) || !detail::get_str16(in, m.n.app_id) || !detail::get_str16(in, m.n.app) ||
            !detail::get_str16(in, m.n.title) || !detail::get_str16(in, m.n.body) || !detail::get_str16(in, m.n.icon)) {
          return std::nullopt;
        }
        return m;
    }
    return std::nullopt;
  }

  // ── rules ──
  /// Apollide's own tray toasts ("client connected" …) would echo back to the client
  inline bool is_own_app(std::string_view app) {
    for (std::string_view own : {"Apollide", "Vibepollo", "Sunshine", "Apollo"}) {
      if (app.find(own) != std::string_view::npos) {
        return true;
      }
    }
    return false;
  }

  /**
   * Someone sits at H1: no stream, the last keyboard/mouse input is recent, and it came after
   * the last stream ended (remote input during a stream also counts as input on Windows).
   * @param since_stream_end_ms -1 when no stream ended since the host started
   */
  inline bool local_use(bool streaming, std::int64_t idle_ms, std::int64_t since_stream_end_ms) {
    if (streaming || idle_ms >= kLocalUseMs) {
      return false;
    }
    return since_stream_end_ms < 0 || idle_ms + kStreamEndGraceMs < since_stream_end_ms;
  }

  /// Indices of stored notifications a client has not seen (oldest first, newest kMaxCatchUp)
  inline std::vector<std::size_t> select_since(const std::vector<stored_t> &items, std::uint32_t host_boot, std::uint32_t client_boot,
                                               std::uint32_t after, std::int64_t now_ms) {
    std::vector<std::size_t> out;
    const bool same = client_boot != 0 && client_boot == host_boot;
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (now_ms - items[i].arrived_ms > kKeepMs) {
        continue;
      }
      if (same && items[i].seq <= after) {
        continue;
      }
      out.push_back(i);
    }
    if (out.size() > kMaxCatchUp) {
      out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(kMaxCatchUp));
    }
    return out;
  }

  inline std::string base64(std::string_view in) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
      const std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) | (static_cast<std::uint8_t>(in[i + 1]) << 8) | static_cast<std::uint8_t>(in[i + 2]);
      out += table[v >> 18];
      out += table[(v >> 12) & 63];
      out += table[(v >> 6) & 63];
      out += table[v & 63];
    }
    if (i + 1 == in.size()) {
      const std::uint32_t v = static_cast<std::uint8_t>(in[i]) << 16;
      out += table[v >> 18];
      out += table[(v >> 12) & 63];
      out += "==";
    } else if (i + 2 == in.size()) {
      const std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) | (static_cast<std::uint8_t>(in[i + 1]) << 8);
      out += table[v >> 18];
      out += table[(v >> 12) & 63];
      out += table[(v >> 6) & 63];
      out += '=';
    }
    return out;
  }
}  // namespace apollide::notify
