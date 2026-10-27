/**
 * @file src/apollide/cursor_channel.h
 * @brief Apollide: send the host cursor shape to clients that draw the cursor themselves.
 *
 * A client (Artemide) that renders its own local cursor asks for this with a control
 * message. From then on the host stops blending its cursor into the video and instead
 * sends the cursor bitmap whenever the shape or visibility changes, so the client shows
 * exactly one cursor with zero added latency and the right shape (I-beam, hand, ...).
 *
 * Control stream messages (little endian, after the 4-byte control header):
 *   0x3A02 client -> host  u8 version(1), u8 enable
 *   0x3A00 host -> client  u8 version(1), u8 reserved, u16 width, u16 height,
 *                          i16 hotspotX, i16 hotspotY, u32 shapeId, RGBA[width*height] (straight alpha)
 *   0x3A01 host -> client  u8 version(1), u8 visible, u32 shapeId
 *
 * 연결: Artemide — 서피스 클라이언트가 이 메시지로 커서 모양을 받아 그린다
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace apollide::cursor {
  constexpr std::uint16_t kShapeMessage = 0x3A00;
  constexpr std::uint16_t kStateMessage = 0x3A01;
  constexpr std::uint16_t kRequestMessage = 0x3A02;

  /// Largest cursor edge sent to clients; bigger cursors are scaled down to fit.
  constexpr int kMaxCursorEdge = 64;

  /**
   * @brief Flag to hand to display capture instead of `&display_cursor`.
   * It mirrors `display_cursor` but turns off while any client draws its own cursor.
   */
  bool *video_cursor_flag();

  /// A client sent kRequestMessage.
  void on_client_request(const void *session, std::string_view payload);

  /// The session is going away.
  void forget(const void *session);

  /// True while at least one client wants cursor shapes (the control loop then polls faster).
  bool active();

  /**
   * @brief Complete plaintext control packets (header + payload) waiting for this session.
   * Returns an empty list when nothing changed since the last call.
   */
  std::vector<std::vector<std::uint8_t>> take_messages(const void *session);
}  // namespace apollide::cursor
