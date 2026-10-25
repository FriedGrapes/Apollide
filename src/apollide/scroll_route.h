/**
 * @file src/apollide/scroll_route.h
 * @brief Apollide: send Artemide's touchpad scrolling as wheel input to classic windows.
 *
 * Artemide turns precision-touchpad scrolling into a two-finger touch pan so that modern
 * apps follow the fingers 1:1. Some classic windows ignore touch pans (for example the
 * firewall "allowed apps" list, hosted inside Explorer) but always scroll with the wheel,
 * and classic windows scroll with the wheel immediately anyway. So when a pan starts over a
 * window that shows a classic scroll bar (on it or a parent; Chromium apps keep theirs off
 * screen and stay on touch), the whole gesture is sent as high-resolution wheel input instead.
 * The same happens while a classic menu is open (a touch pan's release can get lost in the menu
 * loop, and the Claude app then ignores touch pans until it restarts).
 *
 * When the Apollide virtual precision touchpad (Apollide/ptp-driver) is installed, none of that is
 * needed: the two fingers go to it instead, and Windows scrolls every window the way a real
 * precision touchpad would (virtual_ptp.h). The rules above are the fallback without the driver.
 *
 * 연결: Artemide app/streaming/input/scrollpan.cpp — 손가락 번호 0x41D00001/2, 한 칸 = 화면 높이의 6%
 */
#pragma once

#include <cstdint>

namespace apollide::scroll {
  /**
   * @brief Called for every touch event before it is injected.
   * @param pointer_id Touch pointer ID from the client.
   * @param event_type LI_TOUCH_EVENT_*.
   * @param x,y Screen pixel position of the touch.
   * @param video_height Height of the streamed video on the host screen, in pixels.
   * @param video_x,video_y Position of the touch on the streamed video, 0..1.
   * @return true when the event was handled here and must not be injected as touch.
   */
  bool route(std::uint32_t pointer_id, std::uint8_t event_type, long x, long y, long video_height, float video_x,
             float video_y);
}  // namespace apollide::scroll
