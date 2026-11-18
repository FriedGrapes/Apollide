/**
 * @file src/apollide/webrtc_fps_step_math.h
 * @brief Apollide: when a browser (WebRTC) viewer's link stays slow, send every other frame
 *        and give each frame twice the bits, instead of letting text turn into blocks.
 *
 * Why: 2026-11-18 a Kiln viewer on a Mac (over the internet) saw H1's 1600x900@60 screen
 * "crackle". The congestion controller had pushed the shared encoder to 232..2500 kbps; at 60
 * fps that is too few bits per frame for text. 0.3.11 put a hard 4000 kbps floor in, but a link
 * that really is narrower than 4000 would then queue and stall. Now the floor is low (1500) and
 * the picture keeps its sharpness by halving the frame rate while the estimate is low:
 *
 *   - estimate below halve_below_kbps for halve_after  -> 30 fps (every other captured frame)
 *   - estimate at/above restore_at_kbps for restore_after -> back to 60 fps
 *   - brief dips (< halve_after) change nothing, so a one-off wobble does not flicker the rate
 *
 * The encoder's rate control budgets bitrate / configured_fps per frame (NVENC vbv = one frame),
 * so while halved we tell it twice the congestion target: it then spends target / 30 on each of
 * the frames it actually gets, and the wire still carries about the target.
 *
 * Header only so the standalone test can build it without the rest of Sunshine.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>

namespace apollide::webrtc_fps_step {

  using clock = std::chrono::steady_clock;

  /// The congestion controller may not push the encoder below this (or below the session's
  /// configured bitrate, if that is lower).
  inline constexpr int floor_kbps = 1500;
  /// 1600x900 text at 60 fps starts to break up below about this.
  inline constexpr int halve_below_kbps = 5000;
  /// Gap above halve_below_kbps so the rate does not flip back and forth around one number.
  inline constexpr int restore_at_kbps = 8000;
  inline constexpr std::chrono::milliseconds halve_after {2000};
  inline constexpr std::chrono::milliseconds restore_after {5000};

  struct state_t {
    bool halved = false;
    std::optional<clock::time_point> since;  ///< when the estimate started asking for a change
  };

  /** Lower bound for the congestion target of one session. */
  inline int floor_for(int configured_kbps) {
    return std::max(1, std::min(floor_kbps, configured_kbps));
  }

  /**
   * Feed the current congestion target (kbps, before any doubling). Returns true when the
   * halved/full decision changed on this call.
   */
  inline bool step(state_t &state, clock::time_point now, int target_kbps) {
    const bool wants_change = state.halved ? target_kbps >= restore_at_kbps : target_kbps < halve_below_kbps;
    if (!wants_change) {
      state.since.reset();
      return false;
    }
    if (!state.since) {
      state.since = now;
      return false;
    }
    if (now - *state.since < (state.halved ? restore_after : halve_after)) {
      return false;
    }
    state.halved = !state.halved;
    state.since.reset();
    return true;
  }

  /** Bitrate to hand the encoder for this congestion target. */
  inline int encoder_kbps(bool halved, int target_kbps, int configured_kbps) {
    if (!halved) {
      return target_kbps;
    }
    return std::max(target_kbps, std::min(target_kbps * 2, configured_kbps));
  }

  /**
   * Whether the encode loop should leave out a newly captured frame. While halved it keeps
   * frames at least ~two frame intervals apart (a quarter interval of slack for capture jitter).
   * A skipped frame is still converted, so if nothing newer arrives the loop's minimum-FPS
   * timeout encodes it and the last picture is never lost.
   */
  inline bool skip_frame(bool halved, clock::duration since_last_encoded, clock::duration frame_interval) {
    if (!halved) {
      return false;
    }
    return since_last_encoded < frame_interval * 2 - frame_interval / 4;
  }

  /// Set by the WebRTC side, read by the shared encode loop. Only WebRTC sets it, and it is
  /// cleared when the WebRTC capture ends, so Moonlight sessions are never halved.
  inline std::atomic<bool> halved_now {false};

}  // namespace apollide::webrtc_fps_step
