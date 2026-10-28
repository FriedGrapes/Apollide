/**
 * @file src/apollide/resume_mode.cpp
 * @brief Apollide: switch a paused session's virtual display to the resuming client's mode.
 */
#include "resume_mode.h"

#include "resume_mode_math.h"

#include <cstring>
#include <optional>
#include <vector>

#include "src/config.h"
#include "src/display_device.h"
#include "src/logging.h"
#include "src/rtsp.h"
#include "src/stream.h"

#ifdef _WIN32
  #include <windows.h>

  #include "src/platform/windows/utf_utils.h"
  #include "src/platform/windows/virtual_display.h"
#endif

namespace apollide::resume_mode {
  bool take_over_from_other_viewers(const std::string &caller_uuid, bool input_only) {
    const auto sessions = rtsp_stream::get_sessions_snapshot();
    std::vector<std::string> owners;
    owners.reserve(sessions.size());
    for (const auto &session : sessions) {
      owners.push_back(stream::session::uuid(*session));
    }
    std::vector<viewer_t> viewers;
    viewers.reserve(sessions.size());
    for (std::size_t i = 0; i < sessions.size(); ++i) {
      viewers.push_back({owners[i], stream::session::remote_role_match(*sessions[i], remote_session::role_e::game)});
    }
    if (!should_take_over(input_only, caller_uuid, viewers)) {
      return false;
    }

    BOOST_LOG(info) << "Apollide resume take-over: disconnecting the other device watching the app so this one gets the picture";
    return rtsp_stream::disconnect_game_sessions(true);
  }

  bool match_virtual_output(
    const std::string &output_identifier,
    int width,
    int height,
    int refresh_millihz,
    bool nobody_watching,
    bool input_only,
    const std::string &owner_uuid,
    const std::string &client_name
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

    LONG last_result = DISP_CHANGE_SUCCESSFUL;
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
      last_result = result;
      BOOST_LOG(info) << "Apollide resume mode: " << width << 'x' << height << '@' << hz << " refused by "
                      << display_name << " (" << result << ')';
    }
    if (!needs_recreate(last_result)) {
      BOOST_LOG(warning) << "Apollide resume mode: " << display_name << " keeps " << now.width << 'x' << now.height;
      return false;
    }

    // The display only lists standard modes and scaled copies of the mode it was made in, so a
    // client with another aspect ratio needs it made again. Same identity (GUID), new descriptor:
    // Windows sees the same monitor come back in the new mode, and the paused app keeps running.
    if (owner_uuid.empty()) {
      BOOST_LOG(warning) << "Apollide resume mode: no " << width << 'x' << height << " mode and no owner to remake "
                         << display_name << "; it keeps " << now.width << 'x' << now.height;
      return false;
    }
    const auto owner_device = VDISPLAY::resolveActiveVirtualDisplayDeviceIdForStableId(owner_uuid, output_identifier, client_name, false);
    const auto owner_name = owner_device ? display_device::active_display_name_for_device_id(*owner_device) : std::nullopt;
    if (!owner_name || *owner_name != display_name) {
      BOOST_LOG(warning) << "Apollide resume mode: " << display_name << " is not the running app's own virtual display; it keeps "
                         << now.width << 'x' << now.height;
      return false;
    }
    const auto stable_uuid = VDISPLAY::virtualDisplayUuidFromStableId(owner_uuid);
    GUID guid {};
    std::memcpy(&guid, stable_uuid.b8, sizeof(guid));
    const auto made = VDISPLAY::createVirtualDisplay(
      owner_uuid.c_str(), client_name.c_str(), nullptr,
      static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
      static_cast<std::uint32_t>(refresh_millihz), guid,
      static_cast<std::uint32_t>(refresh_millihz), false, 1, std::nullopt, false,
      /*replace_existing=*/true, /*preserve_peer_displays=*/true
    );
    if (!made || !made->device_id) {
      BOOST_LOG(warning) << "Apollide resume mode: could not make " << display_name << " again in " << width << 'x'
                         << height << "; it keeps " << now.width << 'x' << now.height;
      return false;
    }
    // Capture follows the retained output name; point it at the display as it is now
    config::set_runtime_output_name_override(*made->device_id);
    BOOST_LOG(info) << "Apollide resume mode: made the virtual display again " << now.width << 'x' << now.height
                    << " -> " << width << 'x' << height << '@' << whole_hz(refresh_millihz)
                    << " for the resuming client (it had no such mode)";
    return true;
#else
    (void) owner_uuid;
    (void) client_name;
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
