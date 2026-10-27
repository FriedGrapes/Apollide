/**
 * @file src/apollide/resume_mode_math.h
 * @brief Apollide: when a paused virtual-display session is resumed, decide whether the display
 * should switch to the resuming client's mode (header only, so it is testable on its own).
 *
 * The Surface and the Mac ask for different modes. Closing one client's window leaves the app
 * paused on a display in that client's mode; the next client resumes into it. Only the mode of
 * the existing display changes, so the paused app and its windows stay where they are.
 */
#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace apollide::resume_mode {
  struct mode_t {
    int width = 0;
    int height = 0;
    int refresh_millihz = 0;  ///< 59940 for 59.94 Hz, 120000 for 120 Hz
  };

  /// What the resume knows about the situation
  struct situation_t {
    bool nobody_watching = false;  ///< no other stream is showing this display right now
    bool input_only = false;       ///< Remote Input: no picture, never touch displays
    bool virtual_output = false;   ///< capture target is one of our virtual displays (never a physical monitor)
  };

  /// Whole hertz the display reports for a requested rate (59940 → 60, 120000 → 120)
  inline int whole_hz(int refresh_millihz) {
    return (refresh_millihz + 500) / 1000;
  }

  /// true when the display should switch to the requested mode
  inline bool should_switch(const situation_t &s, const mode_t &current, const mode_t &requested) {
    if (!s.nobody_watching || s.input_only || !s.virtual_output) {
      return false;
    }
    if (requested.width <= 0 || requested.height <= 0 || requested.refresh_millihz <= 0) {
      return false;
    }
    if (current.width != requested.width || current.height != requested.height) {
      return true;
    }
    // Windows reports whole hertz; 59.94 and 60 are the same display mode
    return std::abs(whole_hz(current.refresh_millihz) - whole_hz(requested.refresh_millihz)) > 0;
  }

  /// Rates to try, best first (59.94 is listed as 59 or 60 depending on the driver)
  inline std::vector<int> hz_candidates(int refresh_millihz) {
    const int hz = whole_hz(refresh_millihz);
    std::vector<int> out {hz};
    const int floor_hz = refresh_millihz / 1000;
    if (floor_hz != hz && floor_hz > 0) {
      out.push_back(floor_hz);
    }
    return out;
  }
}  // namespace apollide::resume_mode
