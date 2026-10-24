/**
 * @file src/apollide/auto_update.cpp
 * @brief Apollide: quiet self-install of newer releases (see auto_update.h).
 */
#include "auto_update.h"
#include "release_sums.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <curl/curl.h>

#include "src/config.h"
#include "src/crypto.h"
#include "src/httpcommon.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/rtsp.h"
#include "src/system_tray.h"
#include "src/utility.h"

#ifdef _WIN32
  #include <windows.h>
#endif

using namespace std::literals;

namespace apollide::auto_update {
  namespace {
    namespace fs = std::filesystem;

    /// Nobody may have streamed for this long before the service restarts for an update
    constexpr auto kQuietTime = 10min;
    constexpr auto kPoll = 1min;

    std::mutex g_lock;
    update::release_info_t g_pending;  // guarded by g_lock
    std::once_flag g_worker_started;
    std::atomic<bool> g_installing {false};

    fs::path work_dir() {
      return platf::appdata() / "apollide_updates";
    }

    bool disabled() {
      return fs::exists(platf::appdata() / "apollide_no_auto_update");
    }

    std::string hex(const crypto::sha256_t &digest) {
      static constexpr char digits[] = "0123456789abcdef";
      std::string out;
      for (auto b : digest) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
      }
      return out;
    }

    size_t write_to_string(void *contents, size_t size, size_t nmemb, void *userp) {
      static_cast<std::string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
      return size * nmemb;
    }

    bool download(const std::string &url, std::string &out) {
      out.clear();
      CURL *curl = curl_easy_init();
      if (!curl) {
        return false;
      }
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      http::configure_curl_tls(curl);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Apollide-Updater/1.0");
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
      const CURLcode res = curl_easy_perform(curl);
      long code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
      curl_easy_cleanup(curl);
      if (res != CURLE_OK || code < 200 || code >= 300) {
        BOOST_LOG(warning) << "Apollide update: download failed ("sv << curl_easy_strerror(res) << ", HTTP "sv << code << "): "sv << url;
        return false;
      }
      return true;
    }

    const update::asset_info_t *find_asset(const update::release_info_t &release, const std::string &name) {
      for (const auto &asset : release.assets) {
        if (asset.name == name && !asset.download_url.empty()) {
          return &asset;
        }
      }
      return nullptr;
    }

    std::string plain_version(std::string version) {
      if (!version.empty() && version.front() == 'v') {
        version.erase(0, 1);
      }
      return version;
    }

#ifdef _WIN32
    /// Start msiexec outside this service so it survives the service stop it triggers
    bool run_msiexec(const fs::path &msi, const fs::path &log) {
      std::wstring cmd = L"msiexec.exe /i \"" + msi.wstring() + L"\" /qn /norestart /l*v \"" + log.wstring() + L"\"";
      STARTUPINFOW si {};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi {};
      const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
      BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi);
      if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        // Not inside a job that allows breakaway
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags & ~CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
      }
      if (!ok) {
        BOOST_LOG(error) << "Apollide update: couldn't start msiexec (error "sv << GetLastError() << ')';
        return false;
      }
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    }
#endif

    /// Download, verify and start the install. Returns true when msiexec was started.
    bool install(const update::release_info_t &release) {
      const auto version = plain_version(release.version);
      const auto msi_name = "Apollide_v" + version + ".msi";
      const auto *msi = find_asset(release, msi_name);
      const auto *sums = find_asset(release, "SHA256SUMS");
      if (!msi || !sums) {
        BOOST_LOG(warning) << "Apollide update: release "sv << version << " has no "sv << msi_name << " with SHA256SUMS; not installing"sv;
        return false;
      }

      std::string sums_text;
      std::string package;
      if (!download(sums->download_url, sums_text) || !download(msi->download_url, package)) {
        return false;
      }
      const auto expected = expected_sha256(sums_text, msi_name);
      const auto actual = hex(crypto::hash(package));
      if (expected.empty() || expected != actual) {
        BOOST_LOG(error) << "Apollide update: "sv << msi_name << " failed the SHA-256 check; not installing"sv;
        return false;
      }

      std::error_code ec;
      fs::create_directories(work_dir(), ec);
      const auto msi_path = work_dir() / msi_name;
      {
        std::ofstream file(msi_path, std::ios::binary | std::ios::trunc);
        file.write(package.data(), static_cast<std::streamsize>(package.size()));
        if (!file) {
          BOOST_LOG(error) << "Apollide update: couldn't save "sv << msi_path.string();
          return false;
        }
      }
      // Remembered so the next start can say that the update happened
      {
        std::ofstream marker(work_dir() / "installing.txt", std::ios::trunc);
        marker << version;
      }

#ifdef _WIN32
      BOOST_LOG(info) << "Apollide update: installing "sv << version << " quietly; the service restarts now"sv;
      return run_msiexec(msi_path, work_dir() / ("install-" + version + ".log"));
#else
      return false;
#endif
    }

    void worker() {
      auto quiet_since = std::chrono::steady_clock::now();
      while (true) {
        std::this_thread::sleep_for(kPoll);
        const auto now = std::chrono::steady_clock::now();
        if (rtsp_stream::session_count() > 0) {
          quiet_since = now;
          continue;
        }
        if (now - quiet_since < kQuietTime || g_installing.load()) {
          continue;
        }

        update::release_info_t release;
        {
          std::lock_guard lg {g_lock};
          release = g_pending;
        }
        if (release.version.empty() || disabled()) {
          continue;
        }

        g_installing = true;
        if (install(release)) {
          return;  // msiexec stops this service
        }
        // Try again after the next update check found it again
        {
          std::lock_guard lg {g_lock};
          if (g_pending.version == release.version) {
            g_pending = {};
          }
        }
        g_installing = false;
      }
    }
  }  // namespace

  void consider(const update::release_info_t &release) {
    if (release.version.empty()) {
      return;
    }
    {
      std::lock_guard lg {g_lock};
      if (g_pending.version != release.version) {
        BOOST_LOG(info) << "Apollide update: "sv << plain_version(release.version)
                        << " will install by itself after "sv << std::chrono::duration_cast<std::chrono::minutes>(kQuietTime).count()
                        << " minutes without streaming"sv;
      }
      g_pending = release;
    }
    std::call_once(g_worker_started, [] {
      std::thread(worker).detach();
    });
  }

  void announce_if_updated() {
    const auto marker = work_dir() / "installing.txt";
    std::string version;
    {
      std::ifstream file(marker);
      if (!file || !std::getline(file, version)) {
        return;
      }
    }
    std::error_code ec;
    if (version != PROJECT_VERSION) {
      return;  // still waiting for msiexec, or it failed; keep the marker for the log
    }
    fs::remove(marker, ec);
    for (const auto &entry : fs::directory_iterator(work_dir(), ec)) {
      if (entry.path().extension() == ".msi") {
        fs::remove(entry.path(), ec);
      }
    }
    BOOST_LOG(info) << "Apollide update: now running "sv << version;
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    std::thread([version] {
      // The tray icon appears a moment after startup
      std::this_thread::sleep_for(20s);
      const auto text = "Apollide " + version + "(으)로 자동 업데이트했어요";
      system_tray::tray_notify("Apollide 업데이트", text.c_str());
    }).detach();
#endif
  }
}  // namespace apollide::auto_update
