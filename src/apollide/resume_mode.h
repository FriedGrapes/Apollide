/**
 * @file src/apollide/resume_mode.h
 * @brief Apollide: a client resuming a paused session gets its own display mode on the
 * virtual display that is already there (the app and its windows stay put).
 */
#pragma once

#include <string>

namespace apollide::resume_mode {
  /**
   * A client resuming the running app takes its picture over: other devices watching it are
   * disconnected first (the app keeps running, exactly as when they close their window), so the
   * resume continues as an unshared one and `match_virtual_output` can give it its own mode.
   * Remote Monitor / Remote Input streams are left alone. Call with the stream lifecycle lock
   * held (both resume entry points hold it). Returns true when someone was disconnected.
   */
  bool take_over_from_other_viewers(const std::string &caller_uuid, bool input_only);

  /**
   * Switch the virtual display behind `output_identifier` (device id or \\.\DISPLAYn) to the
   * requested mode when nobody is watching it. Physical monitors, Remote Input and a display
   * someone else is streaming are never touched. When the display does not list the mode (another
   * aspect ratio), the running app's own virtual display (`owner_uuid`'s) is made again in that
   * mode and capture is pointed at it. Returns true when the mode was changed.
   */
  bool match_virtual_output(
    const std::string &output_identifier,
    int width,
    int height,
    int refresh_millihz,
    bool nobody_watching,
    bool input_only,
    const std::string &owner_uuid,
    const std::string &client_name
  );
}  // namespace apollide::resume_mode
