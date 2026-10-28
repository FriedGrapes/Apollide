/** @file tools/apollide_notify.cpp
 * @brief Apollide: reads the console user's Windows notifications and hands new ones to the host.
 *
 * Runs in the interactive user's session (SYSTEM cannot read a user's notifications). Polls
 * UserNotificationListener once a second — no package identity needed for this; the change
 * event would need one — and writes each new toast to the host's message pipe
 * (formats in src/apollide/notify_codec.h).
 *
 * Usage: apollide_notify.exe <pipe name> <host pid>
 * 연결: Artemide — 설계 NAS Apollide/_docs/계획/2026-10-25-알림-전달.md
 */
#include "src/apollide/notify_codec.h"

#include <cerrno>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include <wtsapi32.h>

namespace {
  using namespace apollide::notify;
  using namespace std::chrono_literals;
  namespace wf = winrt::Windows::Foundation;
  namespace wgi = winrt::Windows::Graphics::Imaging;
  namespace wss = winrt::Windows::Storage::Streams;
  namespace wun = winrt::Windows::UI::Notifications;

  bool parse_number(const char *value, unsigned long &result) {
    if (!value || !*value || *value == '-') {
      return false;
    }
    char *end = nullptr;
    errno = 0;
    result = std::strtoul(value, &end, 10);
    return errno == 0 && end && *end == '\0';
  }

  std::uint32_t idle_ms() {
    LASTINPUTINFO info {sizeof(info), 0};
    if (!GetLastInputInfo(&info)) {
      return 0xFFFFFFFF;
    }
    return GetTickCount() - info.dwTime;
  }

  /// The app's logo as a 48×48 PNG ("" when the app has none)
  std::string logo_png(const wun::UserNotification &n) {
    try {
      // Calling through an empty WinRT reference is a crash, not an exception
      const auto info = n.AppInfo();
      const auto display = info ? info.DisplayInfo() : nullptr;
      const auto ref = display ? display.GetLogo(wf::Size {48, 48}) : nullptr;
      if (!ref) {
        return {};  // e.g. unpackaged scripts
      }
      const auto in = ref.OpenReadAsync().get();
      if (!in) {
        return {};
      }
      const auto decoder = wgi::BitmapDecoder::CreateAsync(in).get();
      wgi::BitmapTransform transform;
      transform.ScaledWidth(48);
      transform.ScaledHeight(48);
      transform.InterpolationMode(wgi::BitmapInterpolationMode::Fant);
      const auto bitmap = decoder.GetSoftwareBitmapAsync(wgi::BitmapPixelFormat::Bgra8, wgi::BitmapAlphaMode::Premultiplied, transform,
                                                         wgi::ExifOrientationMode::IgnoreExifOrientation, wgi::ColorManagementMode::DoNotColorManage)
                            .get();
      wss::InMemoryRandomAccessStream out;
      const auto encoder = wgi::BitmapEncoder::CreateAsync(wgi::BitmapEncoder::PngEncoderId(), out).get();
      encoder.SetSoftwareBitmap(bitmap);
      encoder.FlushAsync().get();
      const auto size = static_cast<std::uint32_t>(out.Size());
      if (size == 0 || size > kMaxIcon) {
        return {};
      }
      out.Seek(0);
      wss::Buffer buffer(size);
      const auto read = out.ReadAsync(buffer, size, wss::InputStreamOptions::None).get();
      return std::string(reinterpret_cast<const char *>(read.data()), read.Length());
    } catch (...) {
      return {};
    }
  }

  notification_t describe(const wun::UserNotification &n, std::map<std::string, std::string> &logos) {
    notification_t out;
    try {
      if (const auto info = n.AppInfo()) {
        out.app_id = winrt::to_string(info.AppUserModelId());
        if (const auto display = info.DisplayInfo()) {
          out.app = winrt::to_string(display.DisplayName());
        }
      }
    } catch (...) {
    }
    try {
      const auto toast = n.Notification();
      const auto visual = toast ? toast.Visual() : nullptr;
      const auto binding = visual ? visual.GetBinding(wun::KnownNotificationBindings::ToastGeneric()) : nullptr;
      if (binding) {
        bool first = true;
        for (const auto &text : binding.GetTextElements()) {
          const auto s = winrt::to_string(text.Text());
          if (first) {
            out.title = s;
            first = false;
          } else if (!s.empty()) {
            out.body += (out.body.empty() ? "" : "\n") + s;
          }
        }
      }
    } catch (...) {
    }
    auto key = out.app_id.empty() ? out.app : out.app_id;
    auto found = logos.find(key);
    if (found == logos.end()) {
      found = logos.emplace(key, logo_png(n)).first;
    }
    out.icon = found->second;
    return out;
  }
}  // namespace

int main(int argc, char **argv) {
  // --print: show what would be sent, for checking by hand on H1
  // --print-all: also the toasts already in the notification center
  const bool print_all = argc == 2 && std::string(argv[1]) == "--print-all";
  const bool print = print_all || (argc == 2 && std::string(argv[1]) == "--print");
  unsigned long host_pid = 0;
  if (!print && (argc != 3 || !parse_number(argv[2], host_pid) || host_pid == 0)) {
    return 1;
  }
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0 || session != WTSGetActiveConsoleSessionId()) {
    return 2;
  }
  HANDLE pipe = INVALID_HANDLE_VALUE;
  if (!print) {
    pipe = CreateFileA(argv[1], GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      return 4;
    }
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &server_pid) || server_pid != host_pid) {
      CloseHandle(pipe);
      return 4;
    }
  }
  const auto send = [&](const std::vector<std::uint8_t> &message) {
    if (print) {
      const auto m = parse_helper(std::string_view(reinterpret_cast<const char *>(message.data()), message.size()));
      if (m && m->type == helper_type_e::notification) {
        std::printf("[%s | %s] %s / %s (icon %zu bytes, idle %u ms)\n", m->n.app.c_str(), m->n.app_id.c_str(), m->n.title.c_str(), m->n.body.c_str(), m->n.icon.size(), m->idle_ms);
      } else if (m) {
        std::printf("type %d access %d idle %u\n", static_cast<int>(m->type), static_cast<int>(m->access), m->idle_ms);
      }
      std::fflush(stdout);
      return true;
    }
    DWORD written = 0;
    return WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written, nullptr) && written == message.size();
  };

  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  int code = 0;
  try {
    auto listener = wun::Management::UserNotificationListener::Current();
    auto access = listener.GetAccessStatus();
    if (access != wun::Management::UserNotificationListenerAccessStatus::Allowed) {
      access = listener.RequestAccessAsync().get();
    }
    if (access != wun::Management::UserNotificationListenerAccessStatus::Allowed) {
      send(helper_ready(helper_state_e::denied));
      CloseHandle(pipe);
      return 5;
    }
    if (!send(helper_ready(helper_state_e::ok))) {
      CloseHandle(pipe);
      return 4;
    }

    std::set<std::uint32_t> seen;
    std::map<std::string, std::string> logos;
    bool primed = false;
    auto last_beat = std::chrono::steady_clock::now();
    while (true) {
      std::set<std::uint32_t> now_ids;
      bool listed = false;
      try {
        const auto list = listener.GetNotificationsAsync(wun::NotificationKinds::Toast).get();
        listed = true;
        for (const auto &n : list) {
          now_ids.insert(n.Id());
          // Only toasts that arrive while we watch; the ones already in the center stay home
          if ((!primed && !print_all) || seen.contains(n.Id())) {
            continue;
          }
          auto note = describe(n, logos);
          if (is_own_app(note.app) || (note.title.empty() && note.body.empty())) {
            continue;
          }
          if (!send(helper_notification(idle_ms(), note))) {
            code = 4;
            break;
          }
        }
      } catch (const winrt::hresult_error &e) {
        // The notification platform can be briefly unavailable (sign-out, Explorer restart)
        if (print) {
          std::printf("list failed 0x%08X %s\n", static_cast<unsigned>(e.code().value), winrt::to_string(e.message()).c_str());
        }
      } catch (...) {
      }
      if (print && listed && now_ids.size() != seen.size()) {
        std::printf("toasts %zu (was %zu)\n", now_ids.size(), seen.size());
      }
      if (code != 0) {
        break;
      }
      if (listed) {
        seen = std::move(now_ids);
        primed = true;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now - last_beat >= 2s) {
        if (!send(helper_heartbeat(idle_ms()))) {
          code = 4;
          break;
        }
        last_beat = now;
      }
      if (WTSGetActiveConsoleSessionId() != session) {
        break;
      }
      std::this_thread::sleep_for(1s);
    }
  } catch (...) {
    code = 6;
  }
  CloseHandle(pipe);
  return code;
}
