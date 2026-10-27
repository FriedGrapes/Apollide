/**
 * @file src/apollide/resume_mode.h
 * @brief Apollide: a client resuming a paused session gets its own display mode on the
 * virtual display that is already there (the app and its windows stay put).
 */
#pragma once

#include <string>

namespace apollide::resume_mode {
  /**
   * Switch the virtual display behind `output_identifier` (device id or \\.\DISPLAYn) to the
   * requested mode when nobody is watching it. Physical monitors, Remote Input and a display
   * someone else is streaming are never touched. Returns true when the mode was changed.
   */
  bool match_virtual_output(
    const std::string &output_identifier,
    int width,
    int height,
    int refresh_millihz,
    bool nobody_watching,
    bool input_only
  );
}  // namespace apollide::resume_mode
