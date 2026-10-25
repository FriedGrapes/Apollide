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
