/**
 * @file src/apollide/kiln_monitor_math.h
 * @brief Apollide: decisions behind the Kiln work monitor — a virtual monitor Kiln (on this PC)
 * keeps beside the user's screen without a stream. Header only so it can be tested alone.
 */
#pragma once

#include <string>
#include <vector>

namespace apollide::kiln_monitor {
  /** Stable owner identity in the Remote Monitor coordinator (never a paired client). */
  inline constexpr const char *owner_id = "kiln-workmon";
  inline constexpr const char *owner_label = "Kiln";

  struct mode_t {
    int width = 1920;
    int height = 1080;
    int fps = 30;
  };

  /** A sane desktop mode: 640x480 .. 7680x4320, even sizes, 1..240 Hz. */
  inline bool mode_valid(const mode_t &m) {
    return m.width >= 640 && m.width <= 7680 && m.height >= 480 && m.height <= 4320 &&
           m.width % 2 == 0 && m.height % 2 == 0 && m.fps >= 1 && m.fps <= 240;
  }

  /** Only a program on this PC that also holds an API token may change it. */
  inline bool request_allowed(bool loopback, bool authenticated) {
    return loopback && authenticated;
  }

  /**
   * The exclusive layout keeps only the stream's display. When the Kiln monitor is live it stays
   * as a separate (extended) group so a user connecting does not knock it off the desktop.
   * Empty ids mean "nothing to say" — the caller keeps its default.
   */
  inline std::vector<std::vector<std::string>> exclusive_topology(const std::string &stream_device, const std::string &kiln_device) {
    if (stream_device.empty()) return {};
    if (kiln_device.empty() || kiln_device == stream_device) return {{stream_device}};
    return {{stream_device}, {kiln_device}};
  }
}  // namespace apollide::kiln_monitor
