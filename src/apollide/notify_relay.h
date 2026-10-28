/**
 * @file src/apollide/notify_relay.h
 * @brief Apollide: forwards the H1 user's Windows notifications to Artemide clients.
 *
 * A helper in the console user's session (tools/apollide_notify.exe) reads new toasts and hands
 * them over a message pipe. The host keeps the last ten minutes and passes them on — live over
 * the control stream (0x3A20, to sessions that subscribed with 0x3A21) and to the Surface's
 * background helper over HTTPS (/apollide/notifications). Nothing is kept while someone uses H1
 * directly (no stream, recent local input) or while the web UI switch is off.
 *
 * 연결: Artemide — 설계 NAS Apollide/_docs/계획/2026-10-25-알림-전달.md
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "notify_codec.h"

namespace apollide::notify {
  /// Once at startup: keeps the user-session helper running
  void start();

  // ── control stream (stream.cpp) ──
  void on_subscribe(const void *session, std::string_view payload);
  /// Complete control packets waiting for this session; also tells the relay a stream is live
  std::vector<std::vector<std::uint8_t>> take_messages(const void *session);
  void forget(const void *session);

  // ── nvhttp (paired clients) ──
  struct query_reply_t {
    std::uint32_t boot;
    std::uint32_t last;
    bool forwarding;
    helper_state_e helper;
    std::vector<stored_t> items;
  };
  /// has_cursor false: just boot and last, no backlog
  query_reply_t query(bool has_cursor, std::uint32_t client_boot, std::uint32_t after);

  // ── web UI ──
  nlohmann::json settings();
  void set_enabled(bool enabled);
}  // namespace apollide::notify
