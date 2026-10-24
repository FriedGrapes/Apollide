/**
 * @file src/apollide/scroll_route.cpp
 * @brief Apollide: touchpad scroll pans become wheel input over classic windows (see scroll_route.h).
 */
#include "scroll_route.h"
#include "scroll_route_math.h"

#include <mutex>

#include "src/logging.h"
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
      wheel  ///< Send wheel input instead
    };

    std::mutex g_lock;
    mode_e g_mode = mode_e::none;
    long g_last_x = 0;
    long g_last_y = 0;
    float g_rest_x = 0.0f;
    float g_rest_y = 0.0f;
    bool g_swallow_b = false;  ///< Finger B of a wheel gesture is still down
    bool g_logged = false;

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

  bool route(std::uint32_t pointer_id, std::uint8_t event_type, long x, long y, long video_height) {
#ifdef _WIN32
    if (!is_pan_pointer(pointer_id)) {
      return false;
    }
    std::lock_guard lg {g_lock};

    // Finger A leads: it goes down first and decides how the whole gesture is sent
    if (pointer_id == kPanPointerA && event_type == kDown) {
      g_mode = classic_window_at(x, y) ? mode_e::wheel : mode_e::pan;
      g_last_x = x;
      g_last_y = y;
      g_rest_x = g_rest_y = 0.0f;
      g_swallow_b = g_mode == mode_e::wheel;
      if (g_mode == mode_e::wheel && !g_logged) {
        g_logged = true;
        BOOST_LOG(info) << "Apollide scroll: touchpad scrolling over a classic window is sent as wheel input"sv;
      }
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
    return false;
#endif
  }
}  // namespace apollide::scroll
