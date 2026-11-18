/**
 * @file src/apollide/update_link.h
 * @brief Apollide: updates between the host and its Artemide clients.
 *
 * While streaming, the control stream carries the host's update state to the client
 * (0x3A10) and the client's to the host (0x3A11); either side can ask the other to update
 * now (0x3A12 / 0x3A13). Without a stream, the client polls /apollide/status and can start
 * a host update with /apollide/update (nvhttp, paired clients only). The host remembers each
 * client's version so the web UI can show it and request a client update.
 *
 * 연결: Artemide — 설계 NAS Apollide/_docs/계획/2026-10-24-업데이트-연동.md
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "update_link_codec.h"

namespace apollide::link {
  /// Once at startup: lets the auto updater tell connected clients before it installs.
  void start();

  // ── control stream (stream.cpp) ──
  void on_client_hello(const void *session, const std::string &uuid, const std::string &device_name, std::string_view payload);
  void on_update_host_now(const std::string &device_name);
  /// Complete control packets waiting for this session.
  std::vector<std::vector<std::uint8_t>> take_messages(const void *session, const std::string &uuid);
  void forget(const void *session);

  // ── nvhttp (paired clients) ──
  struct status_reply_t {
    std::string current;
    std::string latest;
    int state;
    bool client_update_requested;
  };
  status_reply_t on_status_query(const std::string &uuid, const std::string &device_name, std::string_view client_version,
                                 std::string_view client_latest, int client_state);
  /// False when there is nothing newer to install.
  bool on_update_query(const std::string &device_name);

  // ── web UI ──
  nlohmann::json snapshot();
  bool request_client_update(const std::string &uuid);
}  // namespace apollide::link
