/**
 * @file src/apollide/scroll_route.cpp
 * @brief Apollide: touchpad scroll pans become wheel input over classic windows (see scroll_route.h).
 */
#include "scroll_route.h"
#include "scroll_route_math.h"
#include "virtual_ptp.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <thread>

#include "src/logging.h"
#include "src/platform/common.h"
#ifdef _WIN32
  #include "src/platform/windows/misc.h"
#endif

#ifdef _WIN32
  #include <windows.h>
#endif

using namespace std::literals;

namespace apollide::scroll {
  namespace {
    enum class mode_e {
      none,  ///< No scroll pan in progress
      pan,  ///< Pass the pan through as touch
      wheel,  ///< Send wheel input instead
      ptp  ///< Send the fingers to the virtual precision touchpad
    };

    std::mutex g_lock;
    mode_e g_mode = mode_e::none;
    long g_last_x = 0;
    long g_last_y = 0;
    float g_rest_x = 0.0f;
    float g_rest_y = 0.0f;
    bool g_swallow_b = false;  ///< Finger B of a wheel gesture is still down
    bool g_logged = false;
    bool g_logged_menu = false;
    bool g_logged_wheel_app = false;

    // Virtual precision touchpad: route() sets where the fingers should be, the feeder thread
    // moves them there in 4 ms steps and reports every step, like a real touchpad would
    struct pad_finger_t {
      float x = 0.0f, y = 0.0f;  ///< Reported position
      float target_x = 0.0f, target_y = 0.0f;
      float anchor_x = 0.0f, anchor_y = 0.0f;  ///< Pad position where it went down
      float video_x = 0.0f, video_y = 0.0f;  ///< Video position where it went down
    };
    pad_finger_t g_pad[2];
    bool g_pad_active = false;  ///< Fingers are on the pad
    bool g_pad_lifting = false;  ///< Lift once the fingers reach their target
    std::condition_variable g_pad_wake;
    std::once_flag g_feeder_started;

    // LI_TOUCH_EVENT_* (Limelight.h)
    constexpr std::uint8_t kDown = 0x01;
    constexpr std::uint8_t kUp = 0x02;
    constexpr std::uint8_t kMove = 0x03;
    constexpr std::uint8_t kCancel = 0x04;

#ifdef _WIN32
    bool shows_scrollbar(HWND hwnd, LONG id) {
      SCROLLBARINFO info {};
      info.cbSize = sizeof(info);
      return GetScrollBarInfo(hwnd, id, &info) && scrollbar_visible(info.rgstate[0]);
    }

    /// The window under the fingers, or one of its parents, shows a classic scroll bar
    bool classic_window_at(long x, long y) {
      HWND hwnd = WindowFromPoint(POINT {x, y});
      for (int depth = 0; hwnd && depth < 16; ++depth) {
        if (has_classic_scrollbars(static_cast<std::uint32_t>(GetWindowLongW(hwnd, GWL_STYLE))) &&
            (shows_scrollbar(hwnd, OBJID_VSCROLL) || shows_scrollbar(hwnd, OBJID_HSCROLL))) {
          return true;
        }
        if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)) {
          break;
        }
        hwnd = GetParent(hwnd);
      }
      return false;
    }

    /// A classic menu is open on the foreground thread, or under the fingers
    bool menu_at(long x, long y) {
      GUITHREADINFO info {};
      info.cbSize = sizeof(info);
      if (GetGUIThreadInfo(0, &info) && menu_open(info.flags)) {
        return true;
      }
      wchar_t name[16] {};
      HWND hwnd = WindowFromPoint(POINT {x, y});
      return hwnd && GetClassNameW(hwnd, name, 16) && std::wstring_view {name} == L"#32768";
    }

    /// Apps that scroll as wheel input (config/apollide_wheel_apps.txt, reread every few seconds)
    const std::vector<std::string> &wheel_apps() {
      static std::vector<std::string> apps = kDefaultWheelApps;
      static std::chrono::steady_clock::time_point next_read {};
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_read) {
        next_read = now + 5s;
        std::ifstream file {platf::appdata() / "apollide_wheel_apps.txt", std::ios::binary};
        if (file) {
          const std::string text {std::istreambuf_iterator<char> {file}, {}};
          apps = parse_app_list(text);
        } else {
          apps = kDefaultWheelApps;
        }
      }
      return apps;
    }

    /// Lower-case executable name of the top-level window under the fingers
    std::string app_at(long x, long y) {
      HWND hwnd = WindowFromPoint(POINT {x, y});
      hwnd = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
      DWORD pid = 0;
      if (!hwnd || !GetWindowThreadProcessId(hwnd, &pid) || !pid) {
        return {};
      }
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (!process) {
        return {};
      }
      wchar_t path[MAX_PATH] {};
      DWORD size = MAX_PATH;
      const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
      CloseHandle(process);
      if (!ok) {
        return {};
      }
      std::wstring_view wide {path, size};
      wide = wide.substr(wide.find_last_of(L"\\/") + 1);
      std::string name;
      for (const wchar_t c : wide) {
        name.push_back(c < 0x80 ? static_cast<char>(std::tolower(static_cast<int>(c))) : '?');
      }
      return name;
    }

    /// The app under the fingers is on the wheel list
    bool wheel_app_at(long x, long y) {
      const auto &apps = wheel_apps();
      if (apps.empty()) {
        return false;
      }
      const auto name = app_at(x, y);
      return !name.empty() && std::find(apps.begin(), apps.end(), name) != apps.end();
    }

    /// PTP scan time: 100 µs units, wrapping
    std::uint16_t scan_time_now() {
      return static_cast<std::uint16_t>(GetTickCount64() * 10);
    }

    /// Current finger positions as a frame (call with g_lock held)
    ptp_touchpad_report_t pad_frame(bool touching) {
      ptp_touchpad_report_t frame {};
      for (int i = 0; i < 2; ++i) {
        frame.fingers[i].flags = PTP_FINGER_CONFIDENCE | (touching ? PTP_FINGER_TIP : 0) | PTP_FINGER_ID(i);
        frame.fingers[i].x = static_cast<std::uint16_t>(g_pad[i].x + 0.5f);
        frame.fingers[i].y = static_cast<std::uint16_t>(g_pad[i].y + 0.5f);
      }
      frame.contact_count = 2;
      frame.scan_time = scan_time_now();
      return frame;
    }

    /// Reports the fingers every 4 ms while they are on the pad (own thread)
    void pad_feeder() {
      auto timer = platf::create_high_precision_timer();
      std::unique_lock lock {g_lock};
      for (;;) {
        g_pad_wake.wait(lock, [] {
          return g_pad_active;
        });
        bool settled = true;
        for (auto &finger : g_pad) {
          finger.x = pad_step(finger.x, finger.target_x);
          finger.y = pad_step(finger.y, finger.target_y);
          settled = settled && std::abs(finger.target_x - finger.x) < 1.0f && std::abs(finger.target_y - finger.y) < 1.0f;
        }
        const bool lift = g_pad_lifting && settled;
        ptp::send(pad_frame(!lift));
        if (lift) {
          g_pad_active = false;
          continue;
        }
        lock.unlock();
        timer->sleep_for(4ms);
        lock.lock();
      }
    }

    void send_wheel(DWORD flags, int units) {
      if (units == 0) {
        return;
      }
      INPUT in {};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = flags;
      in.mi.mouseData = static_cast<DWORD>(units);
      if (SendInput(1, &in, sizeof(in)) == 0) {
        // The input desktop changes on UAC prompts and the lock screen
        platf::syncThreadDesktop();
        SendInput(1, &in, sizeof(in));
      }
    }
#endif
  }  // namespace

  bool route(std::uint32_t pointer_id, std::uint8_t event_type, long x, long y, long video_height, float video_x,
             float video_y) {
#ifdef _WIN32
    if (!is_pan_pointer(pointer_id)) {
      return false;
    }
    std::lock_guard lg {g_lock};

    // Finger A leads: it goes down first and decides how the whole gesture is sent
    if (pointer_id == kPanPointerA && event_type == kDown && wheel_app_at(x, y)) {
      // Apps on the wheel list (the Claude app) scroll smoothly only with the wheel
      if (g_pad_active) {
        ptp::send(pad_frame(false));
        g_pad_active = false;
      }
      g_mode = mode_e::wheel;
      g_last_x = x;
      g_last_y = y;
      g_rest_x = g_rest_y = 0.0f;
      g_swallow_b = true;
      if (!g_logged_wheel_app) {
        g_logged_wheel_app = true;
        BOOST_LOG(info) << "Apollide scroll: an app on the wheel list is under the cursor, touchpad scrolling is sent as wheel input"sv;
      }
    } else if (pointer_id == kPanPointerA && event_type == kDown && ptp::available()) {
      g_mode = mode_e::ptp;
      std::call_once(g_feeder_started, [] {
        std::thread {pad_feeder}.detach();
      });
      if (g_pad_active) {
        // The last gesture is still gliding to its end: lift it before the new one starts
        ptp::send(pad_frame(false));
      }
      g_pad_active = false;
      g_pad_lifting = false;
    } else if (pointer_id == kPanPointerA && event_type == kDown) {
      const bool menu = menu_at(x, y);
      g_mode = menu || classic_window_at(x, y) ? mode_e::wheel : mode_e::pan;
      if (menu && !g_logged_menu) {
        g_logged_menu = true;
        BOOST_LOG(info) << "Apollide scroll: a menu is open, touchpad scrolling is sent as wheel input"sv;
      }
      g_last_x = x;
      g_last_y = y;
      g_rest_x = g_rest_y = 0.0f;
      g_swallow_b = g_mode == mode_e::wheel;
      if (g_mode == mode_e::wheel && !g_logged) {
        g_logged = true;
        BOOST_LOG(info) << "Apollide scroll: touchpad scrolling over a classic window is sent as wheel input"sv;
      }
    }

    // Virtual precision touchpad: Windows itself scrolls whatever is under the cursor
    if (g_mode == mode_e::ptp) {
      const int index = pointer_id == kPanPointerA ? 0 : 1;
      auto &finger = g_pad[index];
      if (event_type == kDown) {
        finger.anchor_x = PTP_LOGICAL_MAX_X / 2.0f + (index ? kPadFingerSpread / 2.0f : -kPadFingerSpread / 2.0f);
        finger.anchor_y = PTP_LOGICAL_MAX_Y / 2.0f;
        finger.video_x = video_x;
        finger.video_y = video_y;
        finger.x = finger.target_x = finger.anchor_x;
        finger.y = finger.target_y = finger.anchor_y;
      } else if (event_type == kMove) {
        finger.target_x = pad_target(finger.anchor_x, video_x, finger.video_x, PTP_LOGICAL_MAX_X);
        finger.target_y = pad_target(finger.anchor_y, video_y, finger.video_y, PTP_LOGICAL_MAX_Y);
      }
      // Artemide always sends finger A then finger B: the pair is complete on B
      if (index == 1) {
        if (event_type == kDown) {
          g_pad_active = true;
          g_pad_wake.notify_one();
        } else if (event_type == kUp || event_type == kCancel) {
          g_pad_lifting = true;
          g_mode = mode_e::none;
        }
      }
      return true;
    }

    // Finger B lifts after finger A: keep swallowing it until it is up too
    if (pointer_id == kPanPointerB) {
      const bool swallow = g_swallow_b;
      if (event_type == kUp || event_type == kCancel) {
        g_swallow_b = false;
      }
      return swallow;
    }

    const bool wheel = g_mode == mode_e::wheel;
    if (wheel && event_type == kMove) {
      const float notch = kNotchFraction * static_cast<float>(video_height);
      // Fingers moving down scroll up (positive wheel); fingers moving left scroll right (positive hwheel)
      send_wheel(MOUSEEVENTF_WHEEL, wheel_units(static_cast<float>(y - g_last_y), notch, g_rest_y));
      send_wheel(MOUSEEVENTF_HWHEEL, wheel_units(static_cast<float>(g_last_x - x), notch, g_rest_x));
      g_last_x = x;
      g_last_y = y;
    }
    if (event_type == kUp || event_type == kCancel) {
      g_mode = mode_e::none;
    }
    return wheel;
#else
    (void) pointer_id;
    (void) event_type;
    (void) x;
    (void) y;
    (void) video_height;
    (void) video_x;
    (void) video_y;
    return false;
#endif
  }
}  // namespace apollide::scroll
