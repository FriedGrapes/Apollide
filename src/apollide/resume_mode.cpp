/**
 * @file src/apollide/resume_mode.cpp
 * @brief Apollide: switch a paused session's virtual display to the resuming client's mode.
 */
#include "resume_mode.h"

#include "resume_mode_math.h"

#include <optional>

#include "src/display_device.h"
#include "src/logging.h"

#ifdef _WIN32
  #include <windows.h>

  #include "src/platform/windows/utf_utils.h"
  #include "src/platform/windows/virtual_display.h"
#endif

namespace apollide::resume_mode {
  bool match_virtual_output(
    const std::string &output_identifier,
    int width,
    int height,
    int refresh_millihz,
    bool nobody_watching,
    bool input_only
  ) {
#ifdef _WIN32
    if (output_identifier.empty()) {
      return false;
    }

    situation_t situation {
      .nobody_watching = nobody_watching,
      .input_only = input_only,
      .virtual_output = VDISPLAY::is_virtual_display_output(output_identifier),
    };

    // Device ids (normal virtual outputs) resolve to the GDI name Windows changes modes by
    std::string display_name = output_identifier;
    if (output_identifier.rfind("\\\\.\\", 0) != 0) {
      const auto resolved = display_device::active_display_name_for_device_id(output_identifier);
      if (!resolved) {
        BOOST_LOG(info) << "Apollide resume mode: capture display is not active, mode left as is";
        return false;
      }
      display_name = *resolved;
    }
    const auto wide_name = utf_utils::from_utf8(display_name);

    DEVMODEW current {};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(wide_name.c_str(), ENUM_CURRENT_SETTINGS, &current)) {
      BOOST_LOG(warning) << "Apollide resume mode: could not read the mode of " << display_name;
      return false;
    }

    const mode_t now {
      static_cast<int>(current.dmPelsWidth),
      static_cast<int>(current.dmPelsHeight),
      static_cast<int>(current.dmDisplayFrequency) * 1000,
    };
    const mode_t wanted {width, height, refresh_millihz};
    if (!should_switch(situation, now, wanted)) {
      BOOST_LOG(info) << "Apollide resume mode: " << display_name << " stays " << now.width << 'x' << now.height << '@'
                      << current.dmDisplayFrequency << " (requested " << width << 'x' << height << '@'
                      << whole_hz(refresh_millihz) << ", virtual=" << situation.virtual_output
                      << ", nobody watching=" << nobody_watching << ", input only=" << input_only << ')';
      return false;
    }

    for (const int hz : hz_candidates(refresh_millihz)) {
      DEVMODEW mode = current;
      mode.dmPelsWidth = static_cast<DWORD>(width);
      mode.dmPelsHeight = static_cast<DWORD>(height);
      mode.dmDisplayFrequency = static_cast<DWORD>(hz);
      mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
      const LONG result = ChangeDisplaySettingsExW(wide_name.c_str(), &mode, nullptr, CDS_UPDATEREGISTRY, nullptr);
      if (result == DISP_CHANGE_SUCCESSFUL) {
        BOOST_LOG(info) << "Apollide resume mode: " << display_name << ' ' << now.width << 'x' << now.height << '@'
                        << current.dmDisplayFrequency << " -> " << width << 'x' << height << '@' << hz
                        << " for the resuming client";
        return true;
      }
      BOOST_LOG(info) << "Apollide resume mode: " << width << 'x' << height << '@' << hz << " refused by "
                      << display_name << " (" << result << ')';
    }
    BOOST_LOG(warning) << "Apollide resume mode: the virtual display has no " << width << 'x' << height
                       << " mode; it keeps " << now.width << 'x' << now.height;
    return false;
#else
    (void) output_identifier;
    (void) width;
    (void) height;
    (void) refresh_millihz;
    (void) nobody_watching;
    (void) input_only;
    return false;
#endif
  }
}  // namespace apollide::resume_mode
