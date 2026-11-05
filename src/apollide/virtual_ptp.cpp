/**
 * @file src/apollide/virtual_ptp.cpp
 * @brief Apollide: hand contact frames to the Apollide virtual precision touchpad (see virtual_ptp.h).
 */
#include "virtual_ptp.h"

#include <chrono>
#include <vector>

#include "src/logging.h"

#ifdef _WIN32
  #include <windows.h>
  // windows.h first
  #include <hidsdi.h>
  #include <setupapi.h>
#endif

using namespace std::literals;

namespace apollide::ptp {
#ifdef _WIN32
  namespace {
    HANDLE g_device = INVALID_HANDLE_VALUE;
    std::chrono::steady_clock::time_point g_next_try {};
    bool g_logged = false;

    /// The vendor collection of our device: VID/PID match and usage page 0xFF00, usage 0x01
    HANDLE open_injector() {
      GUID hid;
      HidD_GetHidGuid(&hid);
      HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (set == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
      }
      HANDLE found = INVALID_HANDLE_VALUE;
      SP_DEVICE_INTERFACE_DATA iface {};
      iface.cbSize = sizeof(iface);
      for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &iface); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &need, nullptr);
        std::vector<BYTE> buffer(need);
        auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, need, nullptr, nullptr)) {
          continue;
        }
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
          continue;
        }
        HIDD_ATTRIBUTES attributes {};
        attributes.Size = sizeof(attributes);
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetAttributes(h, &attributes) && attributes.VendorID == PTP_VENDOR_ID &&
            attributes.ProductID == PTP_PRODUCT_ID && HidD_GetPreparsedData(h, &preparsed)) {
          HIDP_CAPS caps {};
          if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0xFF00 && caps.Usage == 0x01) {
            found = h;
          }
          HidD_FreePreparsedData(preparsed);
        }
        if (found != h) {
          CloseHandle(h);
        }
      }
      SetupDiDestroyDeviceInfoList(set);
      return found;
    }
  }  // namespace

  bool available() {
    if (g_device != INVALID_HANDLE_VALUE) {
      return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < g_next_try) {
      return false;
    }
    g_next_try = now + 5s;
    g_device = open_injector();
    if (g_device != INVALID_HANDLE_VALUE && !g_logged) {
      g_logged = true;
      BOOST_LOG(info) << "Apollide scroll: virtual precision touchpad found; touchpad scrolling goes through it"sv;
    }
    return g_device != INVALID_HANDLE_VALUE;
  }

  bool send(const ptp_touchpad_report_t &frame) {
    if (g_device == INVALID_HANDLE_VALUE) {
      return false;
    }
    auto copy = frame;
    copy.report_id = PTP_REPORT_INJECT;
    if (HidD_SetFeature(g_device, &copy, sizeof(copy))) {
      return true;
    }
    BOOST_LOG(warning) << "Apollide scroll: virtual precision touchpad stopped answering ("sv << GetLastError() << ')';
    CloseHandle(g_device);
    g_device = INVALID_HANDLE_VALUE;
    g_next_try = {};
    return false;
  }
#else
  bool available() {
    return false;
  }

  bool send(const ptp_touchpad_report_t &) {
    return false;
  }
#endif
}  // namespace apollide::ptp
