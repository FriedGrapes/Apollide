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

  /// Share of the pad the fingers cross for crossing the whole streamed video. Windows adds its
  /// own touchpad gain (about 2.6x measured on H1 at 1:1), so 0.4 scrolls about as far as the
  /// fingers moved on the Surface.
  constexpr float kPadScale = 0.4f;

  /// Each 4 ms tick the fingers cover this share of the remaining distance to their target, so
  /// sparse network events become a smooth 250 Hz stream like a real touchpad (about 10 ms lag).
  constexpr float kPadFollow = 0.35f;

  /**
   * @brief Pad coordinate of a finger that started at @p anchor_pad when it was at @p video_anchor.
   * @param anchor_pad Where the finger went down on the pad (pad units).
   * @param video_now,video_anchor Current and starting position on the streamed video, 0..1.
   * @param logical_max Pad logical maximum on this axis.
   */
  constexpr float pad_target(float anchor_pad, float video_now, float video_anchor, int logical_max) {
    const float value = anchor_pad + (video_now - video_anchor) * kPadScale * static_cast<float>(logical_max);
    return value < 0.0f ? 0.0f : (value > static_cast<float>(logical_max) ? static_cast<float>(logical_max) : value);
  }

  /// One interpolation tick from @p current toward @p target
  constexpr float pad_step(float current, float target) {
    return current + (target - current) * kPadFollow;
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
