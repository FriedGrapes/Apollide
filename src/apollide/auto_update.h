/**
 * @file src/apollide/auto_update.h
 * @brief Apollide: install a newer Apollide release by itself while nobody is streaming.
 *
 * The upstream update check (update.cpp) only notifies. When it finds a newer
 * FriedGrapes/Apollide release, this module waits until no client has streamed for a
 * while, downloads Apollide_v<version>.msi, verifies it against the release's
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

  /// Called right before msiexec starts, on the installing thread (e.g. to tell connected clients).
  void set_before_install(std::function<void()> callback);

  /// Download, verify and install `release` (blocking).
  void run_install(const update::release_info_t &release);

  /// The update check found `release` (empty version = nothing newer). Called from the check thread.
  void consider(const update::release_info_t &release);

  /// Once at startup: tell the user when the previous run installed this version.
  void announce_if_updated();
}  // namespace apollide::auto_update
