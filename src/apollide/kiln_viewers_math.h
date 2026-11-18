/**
 * @file src/apollide/kiln_viewers_math.h
 * @brief Apollide: who is watching H1's picture, for Kiln's [H1을 이 창 모양으로] button.
 *
 * Kiln changes the size of the one shared capture when a person presses that button. If a real
 * remote client (Moonlight / Artemide over RTSP) is watching, Kiln first asks "that connection
 * will end" and, on [끊고 바꾸기], ends those streams (pairing stays) before it makes its browser
 * session in the new size. The Kiln key may not use the admin routes (/api/session/status,
 * /api/clients/disconnect), so these are its own: GET /api/kiln/viewers and
 * POST /api/kiln/viewers/disconnect, from this PC only.
 *
 * Header only so the standalone test can build it without the rest of Sunshine.
 * 연결: Kiln — /kiln/screen/viewers · /kiln/screen/viewers/disconnect (부모 대화 'H1 화면 작게 보임', 2026-11-05)
 */
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace apollide::kiln_viewers {

  /// One RTSP stream as the stream layer reports it.
  struct stream_t {
    std::string client_uuid;  ///< the paired device's uuid (what /api/clients/disconnect takes)
    std::string device_name;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string role = "game";  ///< game · monitor
    bool input_only = false;  ///< sends input only, no picture
    std::string state;  ///< starting · running · stopping · stopped
  };

  struct paired_t {
    std::string uuid;
    std::string name;
  };

  /// The running app, which stays running (paused) after its client's stream ends.
  struct app_t {
    bool running = false;
    std::string client_uuid;  ///< who launched it (empty for a Web UI launch)
    std::string name;
  };

  struct viewer_t {
    std::string uuid;
    std::string name;
    std::string kind = "moonlight";
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string role = "game";
    bool paused = false;  ///< no stream now, but its app keeps running and it can resume
    std::string app;
  };

  inline std::string name_for(const std::string &uuid, const std::string &fallback, const std::vector<paired_t> &paired) {
    for (const auto &p : paired) {
      if (p.uuid == uuid && !p.name.empty()) {
        return p.name;
      }
    }
    return fallback;
  }

  inline bool live(const stream_t &s) {
    return !s.input_only && (s.state == "running" || s.state == "starting");
  }

  /** Live picture streams, then the paused app's owner when it has no live game stream. */
  inline std::vector<viewer_t> list_viewers(const std::vector<stream_t> &streams, const std::vector<paired_t> &paired, const app_t &app) {
    std::vector<viewer_t> out;
    bool owner_streams = false;
    for (const auto &s : streams) {
      if (!live(s)) {
        continue;
      }
      viewer_t v;
      v.uuid = s.client_uuid;
      v.name = name_for(s.client_uuid, s.device_name, paired);
      v.width = s.width;
      v.height = s.height;
      v.fps = s.fps;
      v.role = s.role;
      if (app.running && s.client_uuid == app.client_uuid) {
        v.app = app.name;
        owner_streams = owner_streams || s.role == "game";
      }
      out.push_back(std::move(v));
    }
    if (app.running && !app.client_uuid.empty() && !owner_streams) {
      viewer_t v;
      v.uuid = app.client_uuid;
      v.name = name_for(app.client_uuid, app.client_uuid, paired);
      v.paused = true;
      v.app = app.name;
      out.push_back(std::move(v));
    }
    return out;
  }

  struct ask_t {
    std::vector<std::string> uuids;
    bool all = false;
  };

  struct pick_t {
    std::vector<std::string> uuids;  ///< live viewers to disconnect, each once
    std::vector<std::string> unknown;  ///< asked for but not streaming now
    bool bad = false;
  };

  /** Which clients to end: {"all":true} or the asked uuids that are streaming now. */
  inline pick_t pick_targets(const ask_t &ask, const std::vector<viewer_t> &viewers) {
    pick_t out;
    const auto add = [&](std::vector<std::string> &list, const std::string &uuid) {
      if (!uuid.empty() && std::find(list.begin(), list.end(), uuid) == list.end()) {
        list.push_back(uuid);
      }
    };
    const auto streaming = [&](const std::string &uuid) {
      return std::any_of(viewers.begin(), viewers.end(), [&](const viewer_t &v) {
        return v.uuid == uuid && !v.paused;
      });
    };
    if (ask.all) {
      for (const auto &v : viewers) {
        if (!v.paused) {
          add(out.uuids, v.uuid);
        }
      }
      return out;
    }
    if (ask.uuids.empty()) {
      out.bad = true;
      return out;
    }
    for (const auto &uuid : ask.uuids) {
      add(streaming(uuid) ? out.uuids : out.unknown, uuid);
    }
    return out;
  }

}  // namespace apollide::kiln_viewers
