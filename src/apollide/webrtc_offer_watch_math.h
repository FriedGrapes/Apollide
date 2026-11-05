/**
 * @file src/apollide/webrtc_offer_watch_math.h
 * @brief Apollide: decides whether a browser (WebRTC) session that never got an offer
 *        should be closed so it stops holding the one shared capture.
 *
 * Why: 2026-11-05 14:20 on H1 a browser asked for 960x1600. Remaking the virtual display
 * took longer than the caller (Kiln relay, 15 s) was willing to wait, so the caller hung up,
 * but the session was still created afterwards. Nobody ever sent it an offer, it kept
 * encoding frames for nobody, and because WebRTC sessions share one capture, every later
 * 1920x1080 request was refused ("Another WebRTC capture is active ...") for five minutes.
 *
 * Header only so the standalone test can build it without the rest of Sunshine.
 */
#pragma once

#include <chrono>

namespace apollide::webrtc_offer_watch {

  // A real viewer posts its offer right after the create answer (well under a second).
  // 25 s leaves room for a slow phone or a busy host, and still frees the capture long
  // before a caller that waits up to 60 s for create would try another size.
  inline constexpr std::chrono::seconds offer_deadline {25};

  struct look_t {
    bool found = false;  ///< the session still exists at the deadline
    bool has_remote_offer = false;  ///< the viewer has posted its SDP offer
  };

  /** True when the session is still there at the deadline but nobody ever offered. */
  inline bool should_close(const look_t &look) {
    return look.found && !look.has_remote_offer;
  }

}  // namespace apollide::webrtc_offer_watch
