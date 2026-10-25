/**
 * @file src/apollide/scroll_route_math.h
 * @brief Apollide: pure helpers for scroll_route (header only, so they are testable on their own).
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace apollide::scroll {
  /// Artemide's scroll-pan fingers (scrollpan.cpp SCROLL_PAN_POINTER_A/B)
  constexpr std::uint32_t kPanPointerA = 0x41D00001;
  constexpr std::uint32_t kPanPointerB = 0x41D00002;

  /// Artemide moves the fingers this fraction of the video height for one wheel notch
  constexpr float kNotchFraction = 0.06f;
  constexpr int kWheelDelta = 120;

  constexpr bool is_pan_pointer(std::uint32_t pointer_id) {
    return pointer_id == kPanPointerA || pointer_id == kPanPointerB;
  }

  /// Window styles WS_VSCROLL / WS_HSCROLL: the window may draw classic scroll bars
  constexpr bool has_classic_scrollbars(std::uint32_t style) {
    return (style & (0x00200000u | 0x00100000u)) != 0;
  }

  /// SCROLLBARINFO.rgstate[0] of a scroll bar that is really on screen. Chromium windows
  /// (Chrome, Electron apps like Claude, Whale) set the styles but keep the bars off screen.
  constexpr bool scrollbar_visible(std::uint32_t state) {
    return (state & (0x00008000u /* STATE_SYSTEM_INVISIBLE */ | 0x00010000u /* STATE_SYSTEM_OFFSCREEN */)) == 0;
  }

  /// GUITHREADINFO.flags of a thread that has a classic menu open (GUI_INMENUMODE, GUI_POPUPMENUMODE).
  /// A touch pan sent while such a menu runs can lose its release inside the menu loop, and
  /// Chromium apps with native menus (the Claude app's right-click menu) then ignore every
  /// later touch pan until they restart. Wheel input has no such state.
  constexpr bool menu_open(std::uint32_t gui_flags) {
    return (gui_flags & (0x00000004u | 0x00000010u)) != 0;
  }

  /// Extra gap between the two contacts on the virtual touchpad (about 1.3 cm), so Windows sees
  /// two clearly separate fingers; Artemide's own fingers are only about 0.9 cm apart there.
  constexpr int kPadFingerSpread = 150;

  /**
   * @brief Contact coordinate on the virtual touchpad for a position on the streamed video.
   * @details The pad has the video's 3:2 shape and covers all of it, so finger travel on the
   *          video becomes the same share of the pad and Windows scrolls about 1:1.
   * @param fraction Position on the video, 0..1.
   * @param logical_max Pad logical maximum on this axis.
   * @param offset Extra pad units (for the second finger).
   */
  constexpr std::uint16_t pad_coord(float fraction, int logical_max, int offset) {
    const float clamped = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
    const int value = static_cast<int>(clamped * static_cast<float>(logical_max) + 0.5f) + offset;
    return static_cast<std::uint16_t>(value < 0 ? 0 : (value > logical_max ? logical_max : value));
  }

  /**
   * @brief Turn finger movement (pixels) into wheel units, keeping the fraction for next time.
   * @param pixels How far the fingers moved since the last event.
   * @param notch_pixels Finger distance for one notch (kNotchFraction of the video height on screen).
   * @param remainder Wheel units not sent yet; updated.
   * @return Whole wheel units to send now (120 = one notch).
   */
  inline int wheel_units(float pixels, float notch_pixels, float &remainder) {
    if (notch_pixels <= 0.0f) {
      return 0;
    }
    remainder += pixels / notch_pixels * kWheelDelta;
    const float whole = std::trunc(remainder);
    remainder -= whole;
    return static_cast<int>(whole);
  }
}  // namespace apollide::scroll
