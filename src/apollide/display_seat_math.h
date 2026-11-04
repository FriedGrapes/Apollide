/**
 * @file src/apollide/display_seat_math.h
 * @brief Apollide: per-client virtual display "seats" — when a client may take its own seat back
 * and what a refused client is told (header only, so it is testable on its own).
 *
 * 11-04 19:42: a browser (WebRTC) capture was showing the Desktop app's virtual display when
 * Artemide quit that app. The app's display seat became "release pending" but stayed referenced
 * by the live browser capture, and while any seat is in that state every reservation is refused —
 * reported as "Virtual display client limit reached" with one seat of four in use, so Artemide
 * could not connect again until the browser views closed.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace apollide::display_seat {
  enum class refusal_e {
    none,
    capacity,         ///< every seat belongs to another client
    held_by_capture,  ///< an ended app's display is still shown by a live capture (browser view)
    busy,             ///< display changes are paused (protected output, shutdown)
  };

  /// The client's own seat is ending but a live capture still shows it: give the seat (same
  /// token, same display) back to that client instead of refusing it. Nothing else changes, so the
  /// capture references stay valid and the browser views keep watching the same display.
  inline bool take_own_seat_back(
    bool normal_game,
    bool release_pending,
    std::size_t capture_references,
    bool display_changes_paused_otherwise
  ) {
    return normal_game && release_pending && capture_references > 0 && !display_changes_paused_otherwise;
  }

  struct holder_t {
    std::string label;
    bool released_but_shown = false;  ///< app ended, a capture still shows its display
  };

  /// "Surface (app ended, still shown by a browser view), Mac"
  inline std::string describe_holders(const std::vector<holder_t> &holders) {
    std::string out;
    for (const auto &h : holders) {
      if (!out.empty()) {
        out += ", ";
      }
      out += h.label.empty() ? std::string {"(unnamed client)"} : h.label;
      if (h.released_but_shown) {
        out += " (app ended, still shown by a browser view)";
      }
    }
    return out;
  }

  /// The 409 status message for a refused virtual-display launch
  inline std::string refusal_message(refusal_e why, const std::string &holders, std::size_t used, std::size_t capacity) {
    switch (why) {
      case refusal_e::held_by_capture:
        return "Virtual display busy: an ended app's display is still shown by a browser (WebRTC) view"
               + (holders.empty() ? std::string {} : " (" + holders + ")")
               + ". Close that view (Kiln H1 screen) or reconnect from the same device";
      case refusal_e::busy:
        return "Virtual display busy: display changes are paused, retry in a moment";
      case refusal_e::capacity:
      default:
        return "Virtual display client limit reached (" + std::to_string(used) + "/" + std::to_string(capacity)
               + (holders.empty() ? std::string {} : ": " + holders) + ")";
    }
  }
}  // namespace apollide::display_seat
