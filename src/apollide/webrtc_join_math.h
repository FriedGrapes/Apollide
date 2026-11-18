/**
 * @file src/apollide/webrtc_join_math.h
 * @brief Apollide: when another browser (WebRTC) viewer asks to join the running capture,
 * decide which app the request means and say which setting differs when it cannot join
 * (header only, so it is testable on its own).
 *
 * 11-04 19:44: Kiln's right pane resumed the paused [Desktop] app, so the shared capture's key
 * carried Desktop's app uuid. Artemide then quit that app (current app -1) while the browser
 * capture kept running. Every new viewer asking for "whatever is on screen" (app_id 0) built a
 * key without an app and was refused with "different video or audio settings" although codec,
 * size, rate and bitrate were identical; resume:true was refused with "No running app".
 */
#pragma once

#include <string>
#include <vector>

namespace apollide::webrtc_join {
  /// A request that names no app (app_id 0) means "join the picture that is on screen now",
  /// so while a capture runs it takes that capture's app identity, even after the app ended.
  inline bool adopt_active_app_identity(int requested_app_id, bool capture_active) {
    return requested_app_id <= 0 && capture_active;
  }

  /// A resume is only impossible when nothing runs *and* no browser capture is live to join.
  inline bool resume_has_nothing_to_join(int current_app_id, bool capture_active) {
    return current_app_id <= 0 && !capture_active;
  }

  struct field_t {
    std::string name;
    std::string now;
    std::string asked;
  };

  /// "bitrate_kbps now 20000, asked 25000; fps now 60, asked 30" — only the fields that differ
  inline std::string describe_differences(const std::vector<field_t> &fields) {
    std::string out;
    for (const auto &f : fields) {
      if (f.now == f.asked) {
        continue;
      }
      if (!out.empty()) {
        out += "; ";
      }
      out += f.name + " now " + f.now + ", asked " + f.asked;
    }
    return out;
  }

  inline std::string codec_name(int video_format) {
    switch (video_format) {
      case 0: return "h264";
      case 1: return "hevc";
      case 2: return "av1";
      default: return "format " + std::to_string(video_format);
    }
  }
}  // namespace apollide::webrtc_join
