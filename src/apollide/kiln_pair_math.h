/**
 * @file src/apollide/kiln_pair_math.h
 * @brief Apollide: one-tap Kiln link — Kiln on this PC asks for a key, a signed-in person approves
 * it on the web page, and Kiln picks the key up once (header only, so it is testable on its own).
 *
 * Kiln used to need a hand-made API token with nine scopes typed in one by one. Now Kiln's
 * [H1 화면 연결] button asks here (loopback only), the web page /v2/kiln-pair shows the same four
 * digits Kiln shows, and [허락] makes a key with exactly those nine scopes for Kiln to collect.
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace apollide::kiln_pair {
  using pair_clock = std::chrono::steady_clock;

  /// How long a request waits for the person, and how long an approved key waits for Kiln
  inline constexpr auto request_life = std::chrono::minutes(5);
  inline constexpr auto pickup_life = std::chrono::minutes(2);

  enum class state_e {
    none,      ///< no such request (never made, or replaced by a newer one)
    pending,   ///< waiting for the person on the web page
    approved,  ///< the key is handed over with this answer
    taken,     ///< the key was already handed over
    denied,    ///< the person said no
    expired,   ///< nobody answered in time, or Kiln never picked the key up
  };

  inline const char *state_name(state_e s) {
    switch (s) {
      case state_e::pending: return "pending";
      case state_e::approved: return "approved";
      case state_e::taken: return "taken";
      case state_e::denied: return "denied";
      case state_e::expired: return "expired";
      default: return "none";
    }
  }

  /// One request at a time: a new request replaces the old one
  struct book_t {
    std::string id;
    std::string code;
    std::string token;
    state_e state = state_e::none;
    pair_clock::time_point made {};
    pair_clock::time_point decided {};

    void start(std::string new_id, std::string new_code, pair_clock::time_point now) {
      id = std::move(new_id);
      code = std::move(new_code);
      token.clear();
      state = state_e::pending;
      made = now;
      decided = {};
    }

    void tick(pair_clock::time_point now) {
      if (state == state_e::pending && now - made > request_life) {
        state = state_e::expired;
      }
      if (state == state_e::approved && now - decided > pickup_life) {
        token.clear();
        state = state_e::expired;
      }
    }

    /// Kiln asks how its request is doing; the key comes out exactly once
    state_e poll(const std::string &asked_id, pair_clock::time_point now, std::string &token_out) {
      tick(now);
      if (asked_id.empty() || asked_id != id) {
        return state_e::none;
      }
      if (state == state_e::approved) {
        token_out = token;
        token.clear();
        state = state_e::taken;
        return state_e::approved;
      }
      return state;
    }

    /// The code the web page should show, when a request is waiting
    std::optional<std::string> pending_code(pair_clock::time_point now) {
      tick(now);
      if (state != state_e::pending) {
        return std::nullopt;
      }
      return code;
    }

    /// Seconds left for the person to answer (0 when nothing is waiting)
    long long seconds_left(pair_clock::time_point now) {
      tick(now);
      if (state != state_e::pending) {
        return 0;
      }
      return std::chrono::duration_cast<std::chrono::seconds>(request_life - (now - made)).count();
    }

    bool approve(const std::string &answered_code, pair_clock::time_point now, std::string key) {
      tick(now);
      if (state != state_e::pending || answered_code != code || key.empty()) {
        return false;
      }
      token = std::move(key);
      state = state_e::approved;
      decided = now;
      return true;
    }

    bool deny(const std::string &answered_code, pair_clock::time_point now) {
      tick(now);
      if (state != state_e::pending || answered_code != code) {
        return false;
      }
      state = state_e::denied;
      decided = now;
      return true;
    }
  };

  struct scope_t {
    std::string path;  ///< route pattern as the token page lists it
    std::vector<std::string> methods;
  };

  /// The nine scopes Kiln needs: browser streaming of H1's screen and the Kiln work monitor.
  /// Never the token or pairing routes, so the Kiln key cannot make keys or approve links.
  inline std::vector<scope_t> kiln_scopes() {
    const std::string session = "/api/webrtc/sessions/([A-Fa-f0-9-]+)";
    return {
      {"/api/kiln/monitor", {"GET", "POST", "DELETE"}},
      {"/api/webrtc/capabilities", {"GET"}},
      {"/api/webrtc/sessions", {"GET", "POST"}},
      {session, {"GET", "DELETE"}},
      {session + "/offer", {"POST"}},
      {session + "/answer", {"GET"}},
      {session + "/ice", {"GET", "POST"}},
      {session + "/ice/stream", {"GET"}},
      {"/api/webrtc/cert", {"GET"}},
    };
  }
}  // namespace apollide::kiln_pair
