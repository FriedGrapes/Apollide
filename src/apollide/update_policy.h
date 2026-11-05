/**
 * @file src/apollide/update_policy.h
 * @brief Apollide: when a waiting release may install by itself, and who may ask for it
 * (header only, so it is testable on its own).
 *
 * Nobody streaming: ten quiet minutes and it installs, as before. Someone streaming (Moonlight or
 * a browser — Kiln's H1 screen): the picture shows "update ready · [update now] [later]" and the
 * install waits for that button or for the night tidy-up: 4-6 a.m., 30 minutes without input.
 *
 * 연결: Kiln — /api/kiln/update (상태 읽기·지금 설치·나중에·지금 확인), tools/apollide.py release
 */
#pragma once

#include <chrono>
#include <string_view>

namespace apollide::update_policy {
  using namespace std::chrono_literals;

  /// Nobody may have streamed for this long before the service restarts for an update
  inline constexpr std::chrono::seconds kQuietTime = 10min;
  /// While someone streams, the night tidy-up waits this long without any input
  inline constexpr std::chrono::seconds kNightIdle = 30min;
  /// Night window in local minutes since midnight: [04:00, 06:00)
  inline constexpr int kNightStart = 4 * 60;
  inline constexpr int kNightEnd = 6 * 60;

  struct situation_t {
    bool pending = false;  ///< a newer release is waiting
    bool watching = false;  ///< a Moonlight or browser stream is on (or starting)
    std::chrono::seconds quiet_for {0};  ///< since a stream was last on
    std::chrono::seconds idle_for {0};  ///< since the last input from a stream or the local desk
    int local_minutes = 0;  ///< local time, minutes since midnight
  };

  enum class action_e {
    wait,
    install
  };

  inline bool in_night_window(int local_minutes) {
    return local_minutes >= kNightStart && local_minutes < kNightEnd;
  }

  inline action_e decide(const situation_t &s) {
    if (!s.pending) {
      return action_e::wait;
    }
    if (!s.watching) {
      return s.quiet_for >= kQuietTime ? action_e::install : action_e::wait;
    }
    // Watching: only the night tidy-up, and only when nobody has touched anything for a while.
    // A busy night just passes; the next night's window tries again.
    return in_night_window(s.local_minutes) && s.idle_for >= kNightIdle ? action_e::install : action_e::wait;
  }

  /// true when the picture should offer "[update now] [later]"
  inline bool should_ask(bool pending, bool watching, bool later_chosen, bool installing) {
    return pending && watching && !later_chosen && !installing;
  }

  enum class route_e {
    status,
    check,
    install,
    later,
    unknown
  };

  inline route_e parse_route(std::string_view action) {
    if (action.empty() || action == "status") {
      return route_e::status;
    }
    if (action == "check") {
      return route_e::check;
    }
    if (action == "install") {
      return route_e::install;
    }
    if (action == "later") {
      return route_e::later;
    }
    return route_e::unknown;
  }

  /**
   * Only programs on this PC. Asking for an update check only reads GitHub, so apollide.py may do
   * it without the API token right after a release; everything else needs the token Kiln holds.
   */
  inline bool route_allowed(bool loopback, bool authenticated, route_e route) {
    if (!loopback || route == route_e::unknown) {
      return false;
    }
    return route == route_e::check || authenticated;
  }

  /// A check asked from this PC at most once a minute
  inline bool check_due(std::chrono::seconds since_last) {
    return since_last >= 60s;
  }
}  // namespace apollide::update_policy
