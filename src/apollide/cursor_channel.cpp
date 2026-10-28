/**
 * @file src/apollide/cursor_channel.cpp
 * @brief Apollide: host cursor shape channel (see cursor_channel.h).
 */
#include "cursor_channel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "src/globals.h"
#include "src/logging.h"

#ifdef _WIN32
  #include <windows.h>

  #include "src/platform/windows/misc.h"
#endif

using namespace std::literals;

namespace apollide::cursor {
  namespace {
    struct shape_t {
      int width = 0;
      int height = 0;
      int hotspot_x = 0;
      int hotspot_y = 0;
      std::vector<std::uint8_t> rgba;
    };

    struct client_t {
      std::uint32_t sent_shape = 0;
      std::uint32_t sent_state = 0;
    };

    std::mutex g_lock;
    std::unordered_map<const void *, client_t> g_clients;
    std::atomic<int> g_client_count {0};

    // Latest cursor as seen by the poller (guarded by g_lock)
    shape_t g_shape;
    std::uint32_t g_shape_id = 0;  // bumps whenever the shape changes
    bool g_visible = true;
    std::uint32_t g_state_id = 0;  // bumps whenever visibility or shape changes

    bool g_video_cursor = true;
    std::once_flag g_poller_started;

    void put16(std::vector<std::uint8_t> &out, std::uint16_t v) {
      out.push_back(static_cast<std::uint8_t>(v & 0xFF));
      out.push_back(static_cast<std::uint8_t>(v >> 8));
    }

    void put32(std::vector<std::uint8_t> &out, std::uint32_t v) {
      for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
      }
    }

    std::vector<std::uint8_t> packet(std::uint16_t type, const std::vector<std::uint8_t> &payload) {
      std::vector<std::uint8_t> out;
      out.reserve(4 + payload.size());
      put16(out, type);
      put16(out, static_cast<std::uint16_t>(payload.size()));
      out.insert(out.end(), payload.begin(), payload.end());
      return out;
    }

#ifdef _WIN32
    /// Render the cursor on black and on white and recover straight-alpha RGBA from the pair.
    /// Inverting (XOR) pixels, like the classic I-beam, become black with a white outline.
    bool extract_shape(HCURSOR handle, shape_t &out) {
      ICONINFO info {};
      if (!GetIconInfo(handle, &info)) {
        return false;
      }

      BITMAP bm {};
      int width = 0;
      int height = 0;
      if (info.hbmColor && GetObject(info.hbmColor, sizeof(bm), &bm)) {
        width = bm.bmWidth;
        height = bm.bmHeight;
      } else if (info.hbmMask && GetObject(info.hbmMask, sizeof(bm), &bm)) {
        width = bm.bmWidth;
        height = bm.bmHeight / 2;  // monochrome: AND mask on top of XOR mask
      }
      const int hotspot_x = static_cast<int>(info.xHotspot);
      const int hotspot_y = static_cast<int>(info.yHotspot);
      if (info.hbmColor) {
        DeleteObject(info.hbmColor);
      }
      if (info.hbmMask) {
        DeleteObject(info.hbmMask);
      }
      if (width <= 0 || height <= 0 || width > 512 || height > 512) {
        return false;
      }

      BITMAPINFO bi {};
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = width;
      bi.bmiHeader.biHeight = -height;  // top-down
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;

      HDC screen = GetDC(nullptr);
      HDC dc = CreateCompatibleDC(screen);
      ReleaseDC(nullptr, screen);
      if (!dc) {
        return false;
      }

      auto render = [&](std::uint8_t background, std::vector<std::uint32_t> &pixels) {
        void *bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
          return false;
        }
        auto old = SelectObject(dc, dib);
        std::memset(bits, background, static_cast<size_t>(width) * height * 4);
        const bool ok = DrawIconEx(dc, 0, 0, handle, width, height, 0, nullptr, DI_NORMAL) != 0;
        GdiFlush();
        pixels.assign(static_cast<std::uint32_t *>(bits), static_cast<std::uint32_t *>(bits) + static_cast<size_t>(width) * height);
        SelectObject(dc, old);
        DeleteObject(dib);
        return ok;
      };

      std::vector<std::uint32_t> on_black;
      std::vector<std::uint32_t> on_white;
      const bool ok = render(0x00, on_black) && render(0xFF, on_white);
      DeleteDC(dc);
      if (!ok) {
        return false;
      }

      const size_t count = static_cast<size_t>(width) * height;
      std::vector<std::uint8_t> rgba(count * 4, 0);
      std::vector<std::uint8_t> inverted(count, 0);
      for (size_t i = 0; i < count; ++i) {
        const std::uint32_t b = on_black[i];
        const std::uint32_t w = on_white[i];
        const int bb = b & 0xFF, bg = (b >> 8) & 0xFF, br = (b >> 16) & 0xFF;
        const int wb = w & 0xFF, wg = (w >> 8) & 0xFF, wr = (w >> 16) & 0xFF;

        if (wg + 16 < bg) {
          // Brighter on black than on white: an inverting pixel
          inverted[i] = 1;
          continue;
        }
        const int alpha = std::clamp(255 - (wg - bg), 0, 255);
        if (alpha == 0) {
          continue;
        }
        rgba[i * 4 + 0] = static_cast<std::uint8_t>(std::min(255, br * 255 / alpha));
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(std::min(255, bg * 255 / alpha));
        rgba[i * 4 + 2] = static_cast<std::uint8_t>(std::min(255, bb * 255 / alpha));
        rgba[i * 4 + 3] = static_cast<std::uint8_t>(alpha);
        (void) wb;
        (void) wr;
      }

      // Inverting pixels: solid black core, white halo so it reads on any background
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const size_t i = static_cast<size_t>(y) * width + x;
          if (inverted[i]) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;
            rgba[i * 4 + 3] = 255;
            continue;
          }
          if (rgba[i * 4 + 3] != 0) {
            continue;
          }
          bool next_to_inverted = false;
          for (int dy = -1; dy <= 1 && !next_to_inverted; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
              const int nx = x + dx, ny = y + dy;
              if (nx >= 0 && ny >= 0 && nx < width && ny < height && inverted[static_cast<size_t>(ny) * width + nx]) {
                next_to_inverted = true;
                break;
              }
            }
          }
          if (next_to_inverted) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = 255;
          }
        }
      }

      // Scale down oversized cursors (nearest neighbour keeps edges crisp)
      int out_w = width;
      int out_h = height;
      if (width > kMaxCursorEdge || height > kMaxCursorEdge) {
        const double scale = static_cast<double>(kMaxCursorEdge) / std::max(width, height);
        out_w = std::max(1, static_cast<int>(width * scale));
        out_h = std::max(1, static_cast<int>(height * scale));
        std::vector<std::uint8_t> scaled(static_cast<size_t>(out_w) * out_h * 4);
        for (int y = 0; y < out_h; ++y) {
          for (int x = 0; x < out_w; ++x) {
            const int sx = std::min(width - 1, static_cast<int>(x / scale));
            const int sy = std::min(height - 1, static_cast<int>(y / scale));
            std::memcpy(&scaled[(static_cast<size_t>(y) * out_w + x) * 4], &rgba[(static_cast<size_t>(sy) * width + sx) * 4], 4);
          }
        }
        rgba.swap(scaled);
        out.hotspot_x = static_cast<int>(hotspot_x * scale);
        out.hotspot_y = static_cast<int>(hotspot_y * scale);
      } else {
        out.hotspot_x = hotspot_x;
        out.hotspot_y = hotspot_y;
      }

      out.width = out_w;
      out.height = out_h;
      out.rgba = std::move(rgba);
      return true;
    }

    void poll_loop() {
      HCURSOR last_handle = nullptr;
      bool last_visible = true;
      auto last_desktop_sync = std::chrono::steady_clock::time_point {};

      while (true) {
        const bool clients = g_client_count.load(std::memory_order_relaxed) > 0;
        {
          std::lock_guard lg {g_lock};
          g_video_cursor = display_cursor && !clients;
        }

        if (!clients) {
          last_handle = nullptr;
          std::this_thread::sleep_for(100ms);
          continue;
        }

        // The input desktop changes on UAC prompts and the lock screen
        const auto now = std::chrono::steady_clock::now();
        if (now - last_desktop_sync > 1s) {
          platf::syncThreadDesktop();
          last_desktop_sync = now;
        }

        CURSORINFO ci {};
        ci.cbSize = sizeof(ci);
        if (GetCursorInfo(&ci)) {
          // CURSOR_SUPPRESSED means "hidden because of touch/pen input". The client keeps
          // drawing its own cursor in that case (touchpad scrolling arrives as touch).
          const bool visible = (ci.flags & (CURSOR_SHOWING | CURSOR_SUPPRESSED)) != 0 && ci.hCursor != nullptr;

          if (visible && ci.hCursor != last_handle) {
            shape_t shape;
            if (extract_shape(ci.hCursor, shape)) {
              std::lock_guard lg {g_lock};
              g_shape = std::move(shape);
              ++g_shape_id;
              ++g_state_id;
              g_visible = true;
            }
            last_handle = ci.hCursor;
            last_visible = true;
          } else if (visible != last_visible) {
            std::lock_guard lg {g_lock};
            g_visible = visible;
            ++g_state_id;
            last_visible = visible;
          }
        }

        std::this_thread::sleep_for(8ms);
      }
    }
#endif

    void start_poller() {
#ifdef _WIN32
      std::call_once(g_poller_started, [] {
        std::thread(poll_loop).detach();
      });
#endif
    }
  }  // namespace

  bool *video_cursor_flag() {
    start_poller();
    return &g_video_cursor;
  }

  void on_client_request(const void *session, std::string_view payload) {
    if (payload.size() < 2 || static_cast<std::uint8_t>(payload[0]) != 1) {
      BOOST_LOG(warning) << "Apollide cursor: ignoring malformed client request ("sv << payload.size() << " bytes)"sv;
      return;
    }
    const bool enable = payload[1] != 0;

    std::lock_guard lg {g_lock};
    if (enable) {
      if (g_clients.emplace(session, client_t {}).second) {
        g_client_count.store(static_cast<int>(g_clients.size()), std::memory_order_relaxed);
        g_video_cursor = false;
        BOOST_LOG(info) << "Apollide cursor: client draws the cursor; stopped blending it into the video"sv;
      }
    } else if (g_clients.erase(session)) {
      g_client_count.store(static_cast<int>(g_clients.size()), std::memory_order_relaxed);
      BOOST_LOG(info) << "Apollide cursor: client stopped drawing the cursor"sv;
    }
    start_poller();
  }

  void forget(const void *session) {
    std::lock_guard lg {g_lock};
    if (g_clients.erase(session)) {
      g_client_count.store(static_cast<int>(g_clients.size()), std::memory_order_relaxed);
    }
  }

  bool active() {
    return g_client_count.load(std::memory_order_relaxed) > 0;
  }

  std::vector<std::vector<std::uint8_t>> take_messages(const void *session) {
    std::vector<std::vector<std::uint8_t>> out;

    std::lock_guard lg {g_lock};
    auto it = g_clients.find(session);
    if (it == g_clients.end()) {
      return out;
    }
    auto &client = it->second;

    if (client.sent_shape != g_shape_id && g_shape.width > 0) {
      std::vector<std::uint8_t> payload;
      payload.reserve(16 + g_shape.rgba.size());
      payload.push_back(1);  // version
      payload.push_back(0);
      put16(payload, static_cast<std::uint16_t>(g_shape.width));
      put16(payload, static_cast<std::uint16_t>(g_shape.height));
      put16(payload, static_cast<std::uint16_t>(static_cast<std::int16_t>(g_shape.hotspot_x)));
      put16(payload, static_cast<std::uint16_t>(static_cast<std::int16_t>(g_shape.hotspot_y)));
      put32(payload, g_shape_id);
      payload.insert(payload.end(), g_shape.rgba.begin(), g_shape.rgba.end());
      out.push_back(packet(kShapeMessage, payload));
      client.sent_shape = g_shape_id;
    }

    if (client.sent_state != g_state_id) {
      std::vector<std::uint8_t> payload;
      payload.push_back(1);  // version
      payload.push_back(g_visible ? 1 : 0);
      put32(payload, g_shape_id);
      out.push_back(packet(kStateMessage, payload));
      client.sent_state = g_state_id;
    }

    return out;
  }
}  // namespace apollide::cursor
