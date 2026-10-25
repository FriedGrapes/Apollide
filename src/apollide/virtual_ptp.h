/**
 * @file src/apollide/virtual_ptp.h
 * @brief Apollide: hand contact frames to the Apollide virtual precision touchpad (Apollide/ptp-driver).
 *
 * With the driver installed, Windows treats the frames like a real precision touchpad and does
 * the scrolling itself for whatever window is under the cursor — browsers, classic lists, menus,
 * pages that ignore touch. Without the driver, available() stays false and callers fall back.
 */
#pragma once

#include <cstdint>

#include "virtual_ptp_report.h"

namespace apollide::ptp {
  /// The driver's injection collection is open (retried every few seconds when it is not)
  bool available();

  /// Sends one frame; false when the device went away (the next available() reopens it)
  bool send(const ptp_touchpad_report_t &frame);
}  // namespace apollide::ptp
