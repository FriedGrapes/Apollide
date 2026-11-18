/**
 * @file src/apollide/auto_update.h
 * @brief Apollide: install a newer Apollide release by itself while nobody is streaming.
 *
 * The upstream update check (update.cpp) only notifies. When it finds a newer
 * FriedGrapes/Apollide release, this module waits until no client (Moonlight or browser) has
 * streamed for a while — or, while someone watches, for the on-screen button or the night tidy-up
 * (update_policy.h) — downloads Apollide_v<version>.msi, verifies it against the release's
 * SHA256SUMS and runs a quiet msiexec install (which restarts the service).
 * A file named `apollide_no_auto_update` in the config folder turns it off.
 *
 * 연결: tools/apollide.py release 가 올리는 GitHub 릴리스(Apollide_vX.Y.Z.msi + SHA256SUMS)
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "src/update.h"

namespace apollide::auto_update {
  enum class state_e : int {
    latest = 0,  ///< Nothing newer
    available = 1,  ///< A newer release is waiting (installs by itself after the quiet time, or now on request)
    downloading = 2,
    installing = 3  ///< msiexec is about to stop this service
  };

  struct status_t {
    std::string current;  ///< This build (PROJECT_VERSION)
    std::string latest;  ///< Newer release, empty when none
    state_e state;
  };

  status_t status();

  /// Install the waiting release now (one click from a client, the web UI or the tray). False when there is none.
  bool install_now();

  /// A Moonlight or browser stream is on (or starting) — the update waits for it.
  bool watching();

  /// Input arrived from a stream (mouse, keys, pad, touch); the night tidy-up waits for 30 quiet minutes.
  void note_input();

  /// true when the picture should offer "update ready · [update now] [later]".
  bool ask();

  /// "Later" for the waiting release: not asked again, installs when nobody streams or in the night tidy-up.
  /// False when nothing is waiting.
  bool choose_later();
  bool later_chosen();

  /// Check for a newer release now (asked from this PC right after a release). False within a minute of the last one.
  bool request_check();

  /// Called right before msiexec starts, on the installing thread (e.g. to tell connected clients).
  void set_before_install(std::function<void()> callback);

  /// Download, verify and install `release` (blocking).
  void run_install(const update::release_info_t &release);

  /// The update check found `release` (empty version = nothing newer). Called from the check thread.
  void consider(const update::release_info_t &release);

  /// Once at startup: tell the user when the previous run installed this version.
  void announce_if_updated();
}  // namespace apollide::auto_update
