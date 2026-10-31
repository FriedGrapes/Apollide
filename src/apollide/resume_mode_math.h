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
#include <string_view>
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

  /// A stream that is showing something right now
  struct viewer_t {
    std::string_view client_uuid;
    bool game = false;  ///< showing the app's picture (not Remote Monitor / Remote Input)
  };

  /**
   * true when a client resuming the app should take its picture over: whoever else is watching
   * it is disconnected (as if they had closed the window) so the display can switch to the new
   * client's mode. One person uses H1 at a time, from whichever device is in hand.
   */
  inline bool should_take_over(bool input_only, std::string_view caller_uuid, const std::vector<viewer_t> &viewers) {
    if (input_only || caller_uuid.empty()) {
      return false;
    }
    for (const auto &v : viewers) {
      if (v.game && v.client_uuid != caller_uuid) {
        return true;
      }
    }
    return false;
  }

  /**
   * true when ChangeDisplaySettingsEx refused the mode because the display does not list it
   * (DISP_CHANGE_BADMODE). A virtual display only lists standard modes and scaled copies of the
   * mode it was made in, so a client with another aspect ratio (1440x1440 -> 1920x1280) needs the
   * display made again. Any other failure leaves the display alone.
   */
  inline bool needs_recreate(long change_result) {
    return change_result == -2;
  }

  /// One try at making the display again
  struct remake_try_t {
    mode_t mode;
    bool fallback = false;  ///< the display's old mode: the resuming client keeps the old picture size
  };

  /**
   * Modes to make the display again in, in order. The first make releases the old display before
   * it creates the new one and can stop right after the release (the driver hands the create back
   * to a retry that a peer-preserving create never gets), so the new mode is tried twice. If both
   * fail, the display comes back in its old mode: a stream must never fall through to a physical
   * monitor (10-31: Surface resume got H1's 3840x2160 desktop with tiny UI).
   */
  inline std::vector<remake_try_t> remake_attempts(const mode_t &wanted, const mode_t &old) {
    std::vector<remake_try_t> out {{wanted, false}, {wanted, false}};
    if (old.width > 0 && old.height > 0 && old.refresh_millihz > 0) {
      out.push_back({old, true});
    }
    return out;
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
