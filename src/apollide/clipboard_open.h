/**
 * @file src/apollide/clipboard_open.h
 * @brief Apollide: open the Windows clipboard with a few retries.
 *
 * Another program (a clipboard manager, the app that just copied) often holds the clipboard
 * for a few milliseconds. A single OpenClipboard attempt then fails and the client's sync is
 * silently lost, which made clipboard sync work only some of the time.
 */
#pragma once

#ifdef _WIN32
  #include <windows.h>

namespace apollide::clipboard {
  /// OpenClipboard, retried for up to about half a second
  inline bool open_with_retry() {
    for (int attempt = 0; attempt < 25; ++attempt) {
      if (OpenClipboard(nullptr)) {
        return true;
      }
      Sleep(20);
    }
    return false;
  }
}  // namespace apollide::clipboard
#endif
